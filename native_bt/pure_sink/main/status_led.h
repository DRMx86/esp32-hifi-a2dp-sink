#pragma once

typedef enum {
    STATUS_LED_OFF = 0,      /* Bluetooth not up yet */
    STATUS_LED_DISCOVERABLE, /* slow blink (1 Hz): visible, no phone connected */
    STATUS_LED_PAIRING,      /* fast blink (~3 Hz): 120 s long-press pairing window */
    STATUS_LED_CONNECTED,    /* solid: phone connected */
} status_led_mode_t;

void status_led_init(void);
void status_led_set(status_led_mode_t mode); /* cheap, ignores unchanged modes */
