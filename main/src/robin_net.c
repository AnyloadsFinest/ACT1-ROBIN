#include "robin_net.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"

static const char *TAG = "robin_net";
static int s_ready;

void RobinNet_Init(void)
{
    if (s_ready != 0) {
        return;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_ready = 1;
    ESP_LOGI(TAG, "network stack ready");
}
