#ifndef PURE_SINK_OLED_DISPLAY_H
#define PURE_SINK_OLED_DISPLAY_H
/* SSD1306 OLED showing the phone's AVRCP metadata (title, artist, album), play state, codec/rate and volume.
 *
 * The host owner (app_main) is the only code that reads Bluetooth/stream state; it hands a small copy to
 * oled_display_publish(). A low-priority task on core 0 (the decoder core, away from the Bluetooth core)
 * renders it and sends only the changed pages over I2C, so the display never calls into the stack and a
 * slow or missing panel cannot stall audio. Without a panel the task only probes the bus every few seconds.
 *
 * Wiring (defaults, override with -D in main/CMakeLists.txt): SDA GPIO21, SCL GPIO19, address 0x3C.
 * GPIO22 is the status LED, so the usual 21/22 pair is not used. */
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "oled_render.h"

#ifndef OLED_GPIO_SDA
#define OLED_GPIO_SDA 21
#endif
#ifndef OLED_GPIO_SCL
#define OLED_GPIO_SCL 19
#endif
#ifndef OLED_I2C_ADDR
#define OLED_I2C_ADDR 0x3C /* 0x3D if the module's address jumper is set the other way */
#endif
#ifndef OLED_I2C_HZ
#define OLED_I2C_HZ 400000
#endif
#ifndef OLED_HEIGHT
#define OLED_HEIGHT 64 /* 64 for 128x64 modules, 32 for 128x32 */
#endif
#ifndef OLED_FLIP
#define OLED_FLIP 0 /* 1 rotates the picture by 180 degrees */
#endif
#ifndef OLED_CONTRAST
#define OLED_CONTRAST 0x7F
#endif
#ifndef OLED_IDLE_OFF_MS
#define OLED_IDLE_OFF_MS 300000u /* panel off after this long without a phone (OLED burn-in); 0 = never */
#endif
#define OLED_TASK_PRIORITY 1 /* below the decoder (19): the display only uses spare time */
#define OLED_TASK_CORE 0
#define OLED_TASK_STACK 4096u

_Static_assert(OLED_HEIGHT == 32 || OLED_HEIGHT == 64, "SSD1306 panels are 128x32 or 128x64");

/* Create the display task. Returns false only if the task could not be created; a missing panel is not an
 * error (it is probed again every few seconds). Call once, early, after the PCM ring has been allocated. */
bool oled_display_init(const char *device_name);
/* Any task, nonblocking (a few microseconds): copy the new state for the display task and wake it. */
void oled_display_publish(const oled_info_t *info);
/* For stack-usage logging. NULL before init. */
TaskHandle_t oled_display_task(void);
#endif
