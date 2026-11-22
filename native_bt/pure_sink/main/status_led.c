#include "status_led.h"

#include <stdbool.h>
#include "driver/gpio.h"
#include "esp_timer.h"

#define STATUS_LED_GPIO        2
#define STATUS_LED_ACTIVE_LOW  1   /* 1: 3V3 -> R -> LED anode, LED cathode -> GPIO2 (lit when pin is low)
                                      0: GPIO22 -> R -> LED anode, LED cathode -> GND (lit when pin is high) */
#define SLOW_TOGGLE_US         500000
#define FAST_TOGGLE_US         150000

static esp_timer_handle_t s_timer;
static volatile status_led_mode_t s_mode = STATUS_LED_OFF;
static bool s_phase;

static inline void led_write(bool on) {
    gpio_set_level(STATUS_LED_GPIO, STATUS_LED_ACTIVE_LOW ? !on : on);
}

static void tick(void *arg) {
    (void)arg;
    if (s_mode != STATUS_LED_DISCOVERABLE && s_mode != STATUS_LED_PAIRING) return;
    s_phase = !s_phase;
    led_write(s_phase);
}

void status_led_init(void) {
    const gpio_config_t io = {
        .pin_bit_mask = 1ULL << STATUS_LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io);
    led_write(false);

    const esp_timer_create_args_t args = { .callback = tick, .name = "status_led" };
    esp_timer_create(&args, &s_timer);
}

void status_led_set(status_led_mode_t mode) {
    if (mode == s_mode || !s_timer) return;
    s_mode = mode;
    (void)esp_timer_stop(s_timer); /* returns an error if not running: fine */
    switch (mode) {
    case STATUS_LED_DISCOVERABLE:
        s_phase = true;
        led_write(true);
        esp_timer_start_periodic(s_timer, SLOW_TOGGLE_US);
        break;
    case STATUS_LED_PAIRING:
        s_phase = true;
        led_write(true);
        esp_timer_start_periodic(s_timer, FAST_TOGGLE_US);
        break;
    case STATUS_LED_CONNECTED:
        led_write(true);
        break;
    default:
        led_write(false);
        break;
    }
}
