// This file is part of ESPreSwitch (https://github.com/ESPresense/ESPreSwitch), a fork of switchbota.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 3.

// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
// General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.

// Status on the plug's two LEDs (GPIO7, GPIO8, active low). If a conversion goes wrong these
// may be the only diagnostic, so every state where the plug can get stuck has its own pattern.
// Nothing erased yet (safe to unplug):
//   slow blink, 1 Hz           looking for WiFi
//   2 blinks, pause            no WiFi; rescue portal up (join espreswitch-xxxxxx)
//   short blip every 2 s       on WiFi, but the ESPreSwitch server isn't answering properly
//   3 blinks, long pause       refused: payload too big for this plug's layout; nothing erased
//   solid                      working: talking to the server
// Flash erased (do NOT unplug):
//   fast blink, 5 Hz           writing the payload
//   5 fast blinks, pause       write failed (WiFi, server or checksum); retrying

#include <stdbool.h>

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "config.h"
#include "led.h"

static volatile led_state_t state = LED_WIFI;

static void led_write(int on)
{
    gpio_set_level(LED1_GPIO, !on);
    gpio_set_level(LED2_GPIO, !on);
}

static void blink(int on_ms, int off_ms)
{
    led_write(1);
    vTaskDelay(pdMS_TO_TICKS(on_ms));
    led_write(0);
    vTaskDelay(pdMS_TO_TICKS(off_ms));
}

static void blinks(int n, int pause_ms)
{
    for (int i = 0; i < n; i++) blink(150, 250);
    vTaskDelay(pdMS_TO_TICKS(pause_ms));
}

static void led_task(void *pvParameters)
{
    while (1) {
        switch (state) {
            case LED_PORTAL:        blinks(2, 1200); break;
            case LED_NO_SERVER:     blink(60, 1940); break;
            case LED_REFUSED:       blinks(3, 2500); break;
            case LED_READY:         led_write(1); vTaskDelay(pdMS_TO_TICKS(200)); break;
            case LED_WRITING:       blink(100, 100); break;
            case LED_WRITE_RETRY:   for (int i = 0; i < 5; i++) blink(100, 100); vTaskDelay(pdMS_TO_TICKS(1000)); break;
            case LED_WIFI:
            default:                blink(500, 500); break;
        }
    }
}

void led_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << LED1_GPIO) | (1ULL << LED2_GPIO),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io);
    led_write(0);
    xTaskCreate(&led_task, "ledTask", 2048, NULL, 3, NULL);
}

void led_set(led_state_t s)
{
    bool erased = state == LED_WRITING || state == LED_WRITE_RETRY;
    // Once flash is erased only the erased patterns may show, whatever WiFi is doing:
    // the one thing that must stay visible is "don't unplug".
    if (erased && s != LED_WRITING && s != LED_WRITE_RETRY) return;
    // A retry before anything is erased isn't an erased state; keep the pre-erase pattern.
    if (!erased && s == LED_WRITE_RETRY) return;
    state = s;
}
