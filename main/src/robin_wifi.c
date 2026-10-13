#include "robin_wifi.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "esp_netif_ip_addr.h"
#include <string.h>

static const char *TAG = "robin_wifi";

#define ACT1_WIFI_SSID     "ACT1-Setup"
#define ACT1_WIFI_PASS     "act1setup"

static esp_netif_t *s_ap_netif;

static void RobinWifi_OnEvent(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)data;

    if (base == WIFI_EVENT && id == WIFI_EVENT_AP_START && s_ap_netif != NULL) {
        esp_netif_ip_info_t ip = { 0 };

        esp_netif_get_ip_info(s_ap_netif, &ip);
        ESP_LOGI(TAG, "AP IP " IPSTR, IP2STR(&ip.ip));
    }
}

void RobinWifi_Start(void)
{
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    wifi_config_t ap_cfg = { 0 };
    esp_err_t err;

    s_ap_netif = esp_netif_create_default_wifi_ap();
    if (s_ap_netif == NULL) {
        ESP_LOGE(TAG, "wifi AP netif failed");
        return;
    }

    err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, RobinWifi_OnEvent, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wifi event register: %s", esp_err_to_name(err));
    }

    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init: %s", esp_err_to_name(err));
        return;
    }

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    strncpy((char *)ap_cfg.ap.ssid, ACT1_WIFI_SSID, sizeof(ap_cfg.ap.ssid) - 1U);
    ap_cfg.ap.ssid_len = (uint8_t)strlen(ACT1_WIFI_SSID);
    strncpy((char *)ap_cfg.ap.password, ACT1_WIFI_PASS, sizeof(ap_cfg.ap.password) - 1U);
    ap_cfg.ap.channel = 6;
    ap_cfg.ap.max_connection = 4;
    ap_cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_start: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "Wi-Fi AP \"%s\"", ACT1_WIFI_SSID);
}
