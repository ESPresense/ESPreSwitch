#pragma once

typedef enum {
    LED_WIFI,         // looking for WiFi
    LED_PORTAL,       // no WiFi, rescue portal up
    LED_NO_SERVER,    // on WiFi, server not answering properly
    LED_REFUSED,      // payload too big for this layout; nothing erased
    LED_READY,        // talking to the server, nothing erased
    LED_WRITING,      // flash erased, writing
    LED_WRITE_RETRY,  // flash erased, last attempt failed, retrying
} led_state_t;

void led_init(void);
void led_set(led_state_t s);  // once erased, only LED_WRITING / LED_WRITE_RETRY take effect;
                              // LED_WRITE_RETRY is ignored until then
