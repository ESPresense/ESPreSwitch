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
#include "lwip/sockets.h"
#include "dhcpserver/dhcpserver.h"
#include "tcpip_adapter.h"

#include "wifi.h"
#include "config.h"
#include "led.h"

static const char *TAG = "wireless";

static EventGroupHandle_t s_wifi_event_group;

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define WIFI_NEW_CONFIG_BIT BIT2  // portal saved a network: close the AP, then join it

static void wifi_task(void *pvParameters);

// Rescue portal: after PORTAL_DELAY_MS without WiFi, also open an AP "espreswitch-xxxxxx" with a
// page at 192.168.4.1 to enter a new network. This matters most after the bootloader is erased:
// the plug is only recoverable while it stays powered, and only over WiFi.
#define PORTAL_DELAY_MS 60000
#define PORTAL_RETRY_MS 30000
static volatile bool portal_active = false;
static httpd_handle_t portal_server = NULL;
static void portal_dhcp_dns(void);

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
            ESP_LOGI(TAG, "event_handler: SYSTEM_EVENT_STA_DISCONNECTED, reason %d", event->event_info.disconnected.reason);
            // try to re-connect; with the portal up, wifi_task retries less often, because each
            // attempt scans every channel and knocks phones off the AP
            if (!portal_active) esp_wifi_connect();
            wifi_set_connected(0);
            led_set(portal_active ? LED_PORTAL : LED_WIFI);
            break;
        
        case SYSTEM_EVENT_AP_START:
            ESP_LOGI(TAG, "event_handler: SYSTEM_EVENT_AP_START");
            if (portal_active) portal_dhcp_dns();
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
// WiFi config is saved to NVS (so a network entered in the portal survives the reboot into ota_1)
// until the erase starts. NVS (0x9000 or 0xE000) is inside the erased region, so from then on it
// must only live in RAM.
void wifi_stop_persisting()
{
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
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

    config.sta.pmf_cfg.capable = true;  // join PMF-optional (WPA2/WPA3 transition) networks with PMF
    ESP_LOGI(TAG, "portal: switching to network %s (password %d chars)", (char *)config.sta.ssid,
             (int)strlen((char *)config.sta.password));
    esp_wifi_set_config(WIFI_IF_STA, &config);
    httpd_resp_set_type(req, "text/html");
    esp_err_t err = httpd_resp_send(req,
        "<p>Saved. This network closes now while the plug joins yours. If it can't join, this network "
        "comes back in about a minute: reconnect and check the name and password.", HTTPD_RESP_USE_STRLEN);
    // Join from wifi_task with the AP closed: with a phone on the AP, the radio splits its time
    // between channels and the WPA handshake times out (reason 15).
    xEventGroupSetBits(s_wifi_event_group, WIFI_NEW_CONFIG_BIT);
    return err;
}

// Captive portal: phones on the rescue AP get 192.168.4.1 as their DNS server, and every A query is
// answered with 192.168.4.1, so their connectivity check lands on our page and they pop it up.
// The plug's own lookups (wohand.com on the real WiFi) don't go through this.
static void portal_dhcp_dns(void)
{
    tcpip_adapter_ip_info_t ip;
    tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_AP, &ip);
    tcpip_adapter_dhcps_stop(TCPIP_ADAPTER_IF_AP);
    dhcps_offer_t offer = OFFER_DNS;
    tcpip_adapter_dhcps_option(TCPIP_ADAPTER_OP_SET, TCPIP_ADAPTER_DOMAIN_NAME_SERVER, &offer, sizeof(offer));
    tcpip_adapter_dns_info_t dns = { 0 };
    dns.ip.type = IPADDR_TYPE_V4;
    dns.ip.u_addr.ip4.addr = ip.ip.addr;
    tcpip_adapter_set_dns_info(TCPIP_ADAPTER_IF_AP, ESP_NETIF_DNS_MAIN, &dns);
    tcpip_adapter_dhcps_start(TCPIP_ADAPTER_IF_AP);
}

static void portal_dns_task(void *pvParameters)
{
    int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = htons(53), .sin_addr.s_addr = htonl(INADDR_ANY) };
    struct timeval tv = { .tv_sec = 1 };
    if (s < 0 || bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "portal: DNS server failed to start");
        if (s >= 0) close(s);
        vTaskDelete(NULL);
        return;
    }
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    uint8_t buf[512];
    while (portal_active) {
        struct sockaddr_in from;
        socklen_t fromlen = sizeof(from);
        int n = recvfrom(s, buf, sizeof(buf) - 16, 0, (struct sockaddr *)&from, &fromlen);
        if (n < 12) continue;
        int i = 12;  // skip the question name
        while (i < n && buf[i]) i += buf[i] + 1;
        if (i + 5 > n) continue;
        bool a = buf[i + 1] == 0 && buf[i + 2] == 1;
        n = i + 5;  // header + one question; drop anything after it
        buf[2] = 0x81; buf[3] = 0x80;          // response, no error
        buf[4] = 0; buf[5] = 1;                // one question
        buf[6] = 0; buf[7] = a ? 1 : 0;        // one answer for A, none otherwise
        buf[8] = buf[9] = buf[10] = buf[11] = 0;
        if (a) {
            uint8_t ans[] = { 0xC0, 0x0C, 0, 1, 0, 1, 0, 0, 0, 60, 0, 4, 192, 168, 4, 1 };
            memcpy(buf + n, ans, sizeof(ans));
            n += sizeof(ans);
        }
        sendto(s, buf, n, 0, (struct sockaddr *)&from, fromlen);
    }
    close(s);
    vTaskDelete(NULL);
}

static void portal_start(void)
{
    // Same suffix as ESPresense's own AP (espresense-xxxxxx): last three bytes of the base MAC,
    // reversed, so both networks name the plug the same way.
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    wifi_config_t ap = { .ap = { .max_connection = 2, .authmode = WIFI_AUTH_OPEN } };
    ap.ap.ssid_len = snprintf((char *)ap.ap.ssid, sizeof(ap.ap.ssid), "espreswitch-%02x%02x%02x", mac[5], mac[4], mac[3]);
    portal_active = true;
    led_set(LED_PORTAL);
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    esp_wifi_set_config(WIFI_IF_AP, &ap);

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;  // every GET gets the page, incl. phones' connectivity checks
    if (httpd_start(&portal_server, &config) == ESP_OK) {
        httpd_uri_t save = { .uri = "/save", .method = HTTP_POST, .handler = portal_save };
        httpd_uri_t get = { .uri = "/*", .method = HTTP_GET, .handler = portal_get };
        httpd_register_uri_handler(portal_server, &save);
        httpd_register_uri_handler(portal_server, &get);
    }
    xTaskCreate(&portal_dns_task, "portalDns", 3072, NULL, 4, NULL);
    ESP_LOGI(TAG, "portal: no WiFi, opened AP %s at http://192.168.4.1", (char *)ap.ap.ssid);
}

static void portal_stop(const char *why)
{
    if (portal_server) httpd_stop(portal_server);
    portal_server = NULL;
    esp_wifi_set_mode(WIFI_MODE_STA);
    portal_active = false;
    led_set(wifi_is_connected() ? LED_READY : LED_WIFI);
    ESP_LOGI(TAG, "portal: closed AP (%s)", why);
}

void wifi_task(void *pvParameters)
{
    while (1) {
        TickType_t wait = (portal_active ? PORTAL_RETRY_MS : PORTAL_DELAY_MS) / portTICK_PERIOD_MS;
        EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_NEW_CONFIG_BIT,
                                               false, false, wait);
        if (bits & WIFI_NEW_CONFIG_BIT) {
            xEventGroupClearBits(s_wifi_event_group, WIFI_NEW_CONFIG_BIT);
            vTaskDelay(1000 / portTICK_PERIOD_MS);  // let the "Saved" page reach the phone
            if (portal_active) portal_stop("joining the new network");
            esp_wifi_disconnect();
            esp_wifi_connect();  // portal reopens after PORTAL_DELAY_MS if this fails
        } else if (bits & WIFI_CONNECTED_BIT) {
            ESP_LOGI(TAG, "networkTask: connected to access point");
            if (portal_active) portal_stop("WiFi back");
            while (wifi_is_connected()) { vTaskDelay(1000 / portTICK_PERIOD_MS); }
        } else if (!portal_active) {
            portal_start();
        } else {
            esp_wifi_connect();  // keep trying the configured network while the portal is up
        }
    }
}

