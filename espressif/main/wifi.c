// This file is part of switchbota (https://github.com/kendallgoto/switchbota/).
// Copyright (c) 2022 Kendall Goto.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 3.

// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
// General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
#include <string.h>

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_http_server.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include "wifi.h"
#include "config.h"
#include "led.h"

static const char *TAG = "wireless";

static EventGroupHandle_t s_wifi_event_group;

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static void wifi_task(void *pvParameters);

// Rescue portal: after PORTAL_DELAY_MS without WiFi, also open an AP "espreswitch-xxxxxx" with a
// page at 192.168.4.1 to enter a new network. This matters most after the bootloader is erased:
// the plug is only recoverable while it stays powered, and only over WiFi.
#define PORTAL_DELAY_MS 60000
#define PORTAL_RETRY_MS 30000
static volatile bool portal_active = false;
static httpd_handle_t portal_server = NULL;

static void wifi_set_connected(uint8_t c) {
    if (c) {
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    } else {
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }

}

esp_err_t event_handler(void *ctx, system_event_t *event)
{
    switch(event->event_id) {
        case SYSTEM_EVENT_STA_START:
            ESP_LOGI(TAG, "event_handler: SYSTEM_EVENT_STA_START");
            esp_wifi_connect();
            break;
            
        case SYSTEM_EVENT_STA_GOT_IP:
            ESP_LOGI(TAG, "event_handler: SYSTEM_EVENT_STA_GOT_IP");
            ip_event_got_ip_t* ip_event = (ip_event_got_ip_t*) &event->event_info;
            ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&ip_event->ip_info.ip));
            wifi_set_connected(1);
            led_set(LED_READY);
            break;
            
        case SYSTEM_EVENT_STA_DISCONNECTED:
            ESP_LOGI(TAG, "event_handler: SYSTEM_EVENT_STA_DISCONNECTED");
            // try to re-connect; with the portal up, wifi_task retries less often, because each
            // attempt scans every channel and knocks phones off the AP
            if (!portal_active) esp_wifi_connect();
            wifi_set_connected(0);
            led_set(portal_active ? LED_PORTAL : LED_WIFI);
            break;
        
        case SYSTEM_EVENT_AP_START:
            ESP_LOGI(TAG, "event_handler: SYSTEM_EVENT_AP_START");
            break;
        
        case SYSTEM_EVENT_AP_STOP:
            ESP_LOGI(TAG, "event_handler: SYSTEM_EVENT_AP_STADISCONNECTED");
            break;
            
        case SYSTEM_EVENT_AP_STACONNECTED:
            ESP_LOGI(TAG, "event_handler: SYSTEM_EVENT_AP_STACONNECTED");
            break;
        
        case SYSTEM_EVENT_AP_STADISCONNECTED:
            ESP_LOGI(TAG, "event_handler: SYSTEM_EVENT_AP_STADISCONNECTED");
            break;
        
        case SYSTEM_EVENT_AP_PROBEREQRECVED:
            ESP_LOGI(TAG, "event_handler: SYSTEM_EVENT_AP_STADISCONNECTED");
            break;
            
        default:
            break;        
    }
    return ESP_OK;
}
int wifi_is_connected()
{
    return xEventGroupGetBits(s_wifi_event_group) & WIFI_CONNECTED_BIT;
}
void wifi_connect()
{
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    // get default config ...
    wifi_config_t existing_config = { 0 };
    esp_wifi_get_config(WIFI_IF_STA, &existing_config);
    // Never write WiFi config to NVS: 0x9000 is inside the region being erased and rewritten.
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_LOGI(TAG, "%s", existing_config.sta.ssid);
    if(strlen((char *)existing_config.sta.ssid) == 0 || strcmp((char *)existing_config.sta.ssid, "wocao_factory_test") == 0) {
        ESP_LOGI(TAG, "defaulting to fallback network ...");
        wifi_config_t fallback_config = {
            .sta = {
                .ssid = FALLBACK_SSID,
                .password = FALLBACK_PASS
            }
        };
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &fallback_config));
    }
    
    ESP_ERROR_CHECK(esp_wifi_start());
}

void wifi_init()
{
    ESP_LOGI(TAG, "wifi_init");
    
    ESP_ERROR_CHECK( esp_event_loop_init(event_handler, NULL) );
    tcpip_adapter_init();

    s_wifi_event_group = xEventGroupCreate();
    xTaskCreate(&wifi_task, "wifiTask", 32768, NULL, 5, NULL);
    wifi_connect();
}

// Decode an application/x-www-form-urlencoded value in place.
static void url_decode(char *s)
{
    char *out = s;
    for (; *s; s++) {
        if (*s == '+') {
            *out++ = ' ';
        } else if (*s == '%' && s[1] && s[2]) {
            char hex[3] = { s[1], s[2], 0 };
            *out++ = (char)strtol(hex, NULL, 16);
            s += 2;
        } else {
            *out++ = *s;
        }
    }
    *out = 0;
}

static const char PORTAL_PAGE[] =
    "<!doctype html><meta name=viewport content='width=device-width'><title>ESPreSwitch</title>"
    "<h3>ESPreSwitch</h3><p>This plug can't reach WiFi. Keep it plugged in and enter a network "
    "that can reach the ESPreSwitch server.</p>"
    "<form method=post action=/save><p><input name=s placeholder='WiFi name' required>"
    "<p><input name=p type=password placeholder='Password'><p><button>Connect</button></form>";

static esp_err_t portal_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, PORTAL_PAGE, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t portal_save(httpd_req_t *req)
{
    char body[256] = { 0 };
    int len = httpd_req_recv(req, body, sizeof(body) - 1);
    if (len <= 0) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no form data");
    body[len] = 0;

    wifi_config_t config = { 0 };
    char value[100];
    if (httpd_query_key_value(body, "s", value, sizeof(value)) != ESP_OK || !value[0])
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "WiFi name missing");
    url_decode(value);
    strlcpy((char *)config.sta.ssid, value, sizeof(config.sta.ssid));
    if (httpd_query_key_value(body, "p", value, sizeof(value)) == ESP_OK) {
        url_decode(value);
        strlcpy((char *)config.sta.password, value, sizeof(config.sta.password));
    }

    ESP_LOGI(TAG, "portal: switching to network %s", (char *)config.sta.ssid);
    esp_wifi_disconnect();
    esp_wifi_set_config(WIFI_IF_STA, &config);
    esp_wifi_connect();
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, "<p>Connecting. If it works, this network disappears and the flash carries on.",
                           HTTPD_RESP_USE_STRLEN);
}

static void portal_start(void)
{
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_AP, mac);
    wifi_config_t ap = { .ap = { .max_connection = 2, .authmode = WIFI_AUTH_OPEN } };
    ap.ap.ssid_len = snprintf((char *)ap.ap.ssid, sizeof(ap.ap.ssid), "espreswitch-%02x%02x%02x", mac[3], mac[4], mac[5]);
    portal_active = true;
    led_set(LED_PORTAL);
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    esp_wifi_set_config(WIFI_IF_AP, &ap);

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    if (httpd_start(&portal_server, &config) == ESP_OK) {
        httpd_uri_t get = { .uri = "/", .method = HTTP_GET, .handler = portal_get };
        httpd_uri_t save = { .uri = "/save", .method = HTTP_POST, .handler = portal_save };
        httpd_register_uri_handler(portal_server, &get);
        httpd_register_uri_handler(portal_server, &save);
    }
    ESP_LOGI(TAG, "portal: no WiFi, opened AP %s at http://192.168.4.1", (char *)ap.ap.ssid);
}

static void portal_stop(void)
{
    if (portal_server) httpd_stop(portal_server);
    portal_server = NULL;
    esp_wifi_set_mode(WIFI_MODE_STA);
    portal_active = false;
    led_set(wifi_is_connected() ? LED_READY : LED_WIFI);
    ESP_LOGI(TAG, "portal: WiFi back, closed AP");
}

void wifi_task(void *pvParameters)
{
    while (1) {
        TickType_t wait = (portal_active ? PORTAL_RETRY_MS : PORTAL_DELAY_MS) / portTICK_PERIOD_MS;
        EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, false, true, wait);
        if (bits & WIFI_CONNECTED_BIT) {
            ESP_LOGI(TAG, "networkTask: connected to access point");
            if (portal_active) portal_stop();
            while (wifi_is_connected()) { vTaskDelay(1000 / portTICK_PERIOD_MS); }
        } else if (!portal_active) {
            portal_start();
        } else {
            esp_wifi_connect();  // keep trying the configured network while the portal is up
        }
    }
}

