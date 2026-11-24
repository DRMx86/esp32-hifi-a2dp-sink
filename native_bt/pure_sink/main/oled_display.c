/* SSD1306 driver and display task: see oled_display.h. Text layout is in oled_render.c. */
#include "oled_display.h"

#include <stdio.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"

#define PAGES (OLED_HEIGHT / 8u)
#define FRAME_MS 80u      /* redraw interval while text scrolls */
#define POLL_MS 250u      /* otherwise the task only wakes for new info (and to time the idle power-off) */
#define RETRY_MS 5000u    /* panel missing or bus error: probe again */
#define REFRESH_MS 30000u /* rewrite every page now and then so an I2C glitch cannot leave garbage on screen */
#define XFER_TIMEOUT_MS 100

static const char *TAG = "oled";

static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static oled_info_t pending; /* written by publish(), read by the task; both under 'lock' */
static uint32_t pending_rev;
static TaskHandle_t task;
static char device_name[OLED_NAME_CHARS + 1];

static i2c_master_bus_handle_t bus;
static i2c_master_dev_handle_t dev;
static uint8_t fb[PAGES][OLED_WIDTH];
static uint32_t sent_hash[PAGES];
static uint8_t tx[1 + OLED_WIDTH]; /* 0x40 data control byte + one page */

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

/* ------------------------------------------------------------------ SSD1306 */
static bool command(const uint8_t *cmd, size_t n) {
    uint8_t buf[1 + 32];
    if (n > sizeof buf - 1) return false;
    buf[0] = 0x00; /* control byte: the rest of the transfer is commands */
    memcpy(buf + 1, cmd, n);
    return i2c_master_transmit(dev, buf, n + 1, XFER_TIMEOUT_MS) == ESP_OK;
}

static bool send_page(unsigned page) {
    /* Horizontal addressing: window = columns 0..127 of this page, then stream 128 bytes. */
    const uint8_t window[] = {0x21, 0x00, (uint8_t)(OLED_WIDTH - 1u), 0x22, (uint8_t)page, (uint8_t)page};
    if (!command(window, sizeof window)) return false;
    tx[0] = 0x40;
    memcpy(tx + 1, fb[page], OLED_WIDTH);
    return i2c_master_transmit(dev, tx, sizeof tx, XFER_TIMEOUT_MS) == ESP_OK;
}

static uint32_t page_hash(const uint8_t *p) {
    uint32_t h = 2166136261u; /* FNV-1a */
    for (unsigned i = 0; i < OLED_WIDTH; ++i) h = (h ^ p[i]) * 16777619u;
    return h;
}

/* Send the pages that changed since they were last sent (all of them if force). */
static bool flush(bool force) {
    for (unsigned p = 0; p < PAGES; ++p) {
        uint32_t h = page_hash(fb[p]);
        if (!force && h == sent_hash[p]) continue;
        if (!send_page(p)) return false;
        sent_hash[p] = h;
    }
    return true;
}

static bool set_power(bool on) {
    const uint8_t c = on ? 0xAF : 0xAE;
    return command(&c, 1);
}

static bool panel_init(void) {
    static const uint8_t init[] = {
        0xAE,                           /* display off while RAM is cleared */
        0xD5, 0x80,                     /* clock divide / oscillator */
        0xA8, (uint8_t)(OLED_HEIGHT - 1), /* multiplex ratio */
        0xD3, 0x00,                     /* display offset */
        0x40,                           /* start line 0 */
        0x8D, 0x14,                     /* internal charge pump on */
        0x20, 0x00,                     /* horizontal addressing mode */
        OLED_FLIP ? 0xA0 : 0xA1,        /* segment remap */
        OLED_FLIP ? 0xC0 : 0xC8,        /* COM scan direction */
        0xDA, OLED_HEIGHT == 64 ? 0x12 : 0x02, /* COM pin layout */
        0x81, OLED_CONTRAST,            /* contrast */
        0xD9, 0xF1,                     /* pre-charge */
        0xDB, 0x40,                     /* VCOMH level */
        0xA4,                           /* show RAM content */
        0xA6,                           /* not inverted */
        0x2E,                           /* scrolling off */
    };
    if (!command(init, sizeof init)) return false;
    memset(fb, 0, sizeof fb);
    return flush(true) && set_power(true);
}

/* Probe the bus and (re)initialize the panel. Logs once per failure episode, not every retry. */
static bool bring_up(bool *warned) {
    if (!bus) {
        const i2c_master_bus_config_t cfg = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = (gpio_num_t)OLED_GPIO_SDA,
            .scl_io_num = (gpio_num_t)OLED_GPIO_SCL,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags = {.enable_internal_pullup = true}, /* backup only: modules normally have their own */
        };
        esp_err_t err = i2c_new_master_bus(&cfg, &bus);
        if (err != ESP_OK) {
            bus = NULL;
            if (!*warned) ESP_LOGW(TAG, "I2C bus setup failed (%s): display disabled", esp_err_to_name(err));
            *warned = true;
            return false;
        }
    }
    esp_err_t err = i2c_master_probe(bus, OLED_I2C_ADDR, XFER_TIMEOUT_MS);
    if (err != ESP_OK) {
        if (!*warned)
            ESP_LOGW(TAG, "no display at 0x%02x (SDA GPIO%d, SCL GPIO%d): %s; trying again every %u s", OLED_I2C_ADDR,
                     OLED_GPIO_SDA, OLED_GPIO_SCL,
                     err == ESP_ERR_NOT_FOUND ? "not found" : err == ESP_ERR_TIMEOUT ? "bus timeout, check the pull-ups" : esp_err_to_name(err),
                     (unsigned)(RETRY_MS / 1000u));
        *warned = true;
        return false;
    }
    if (!dev) {
        const i2c_device_config_t dc = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = OLED_I2C_ADDR,
            .scl_speed_hz = OLED_I2C_HZ,
        };
        if (i2c_master_bus_add_device(bus, &dc, &dev) != ESP_OK) {
            dev = NULL;
            if (!*warned) ESP_LOGW(TAG, "cannot add the display to the I2C bus");
            *warned = true;
            return false;
        }
    }
    if (!panel_init()) {
        if (!*warned) ESP_LOGW(TAG, "display did not accept the init sequence");
        *warned = true;
        return false;
    }
    ESP_LOGI(TAG, "SSD1306 128x%u at 0x%02x, SDA GPIO%d SCL GPIO%d, %u kHz", (unsigned)OLED_HEIGHT, OLED_I2C_ADDR,
             OLED_GPIO_SDA, OLED_GPIO_SCL, (unsigned)(OLED_I2C_HZ / 1000u));
    *warned = false;
    return true;
}

/* ------------------------------------------------------------------- task */
static void display_task(void *unused) {
    (void)unused;
    static oled_scene_t scene;
    static oled_info_t info;
    uint32_t seen_rev = 0, idle_since = 0, last_full = 0;
    bool ready = false, warned = false, on = false, force = true, idle_timing = false;

    oled_scene_init(&scene, device_name);
    info.play_status = OLED_PLAY_UNKNOWN;

    for (;;) {
        uint32_t now = now_ms();
        if (!ready) {
            if (!bring_up(&warned)) {
                (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(RETRY_MS));
                continue;
            }
            ready = on = force = true;
            last_full = now;
            idle_timing = false;
        }

        portENTER_CRITICAL(&lock);
        if (pending_rev != seen_rev) {
            info = pending;
            seen_rev = pending_rev;
        }
        portEXIT_CRITICAL(&lock);

        /* Power the panel off after a long time without a phone, back on as soon as there is one. */
        const bool idle = info.link == OLED_LINK_IDLE && !info.pairing;
        if (!idle) {
            idle_timing = false;
        } else if (!idle_timing) {
            idle_timing = true;
            idle_since = now;
        }
        const bool want_on = !(OLED_IDLE_OFF_MS && idle && now - idle_since >= OLED_IDLE_OFF_MS);
        if (want_on != on) {
            if (!set_power(want_on)) {
                ESP_LOGW(TAG, "I2C write failed, will re-initialize");
                (void)i2c_master_bus_reset(bus);
                ready = false;
                continue;
            }
            on = want_on;
        }

        bool animating = false;
        if (on) {
            const bool changed = oled_scene_update(&scene, &info, now);
            animating = oled_scene_animating(&scene, PAGES);
            const bool full = force || now - last_full >= REFRESH_MS;
            if (changed || animating || full) {
                oled_scene_draw(&scene, &fb[0][0], PAGES, now);
                if (!flush(full)) {
                    ESP_LOGW(TAG, "I2C write failed, will re-initialize");
                    (void)i2c_master_bus_reset(bus);
                    ready = false;
                    continue;
                }
                if (full) last_full = now;
                force = false;
            }
        }
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(animating ? FRAME_MS : POLL_MS));
    }
}

bool oled_display_init(const char *name) {
    if (task) return true;
    snprintf(device_name, sizeof device_name, "%s", name ? name : "");
    if (xTaskCreatePinnedToCore(display_task, "oled", OLED_TASK_STACK, NULL, OLED_TASK_PRIORITY, &task, OLED_TASK_CORE) !=
        pdPASS) {
        task = NULL;
        return false;
    }
    return true;
}

void oled_display_publish(const oled_info_t *info) {
    if (!task || !info) return;
    portENTER_CRITICAL(&lock);
    pending = *info;
    ++pending_rev;
    portEXIT_CRITICAL(&lock);
    xTaskNotifyGive(task);
}

TaskHandle_t oled_display_task(void) { return task; }
