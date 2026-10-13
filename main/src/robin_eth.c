#include "robin_eth.h"

#include "board.h"
#include "params.h"
#include "driver/gpio.h"
#include "esp_eth.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "ethernet_init.h"
#include "robin_net.h"
#include "lwip/ip4_addr.h"
#include "sdkconfig.h"
#include "w5500_probe.h"
#include "esp_timer.h"
#include <inttypes.h>

static const char *TAG = "robin_eth";

static esp_netif_t *s_eth_netif;
static esp_eth_handle_t s_eth_handle;
static esp_timer_handle_t s_phy_timer;

static void RobinEth_LogPhy(const char *note)
{
    uint32_t phycfg_raw = 0;

    if (s_eth_handle == NULL) {
        ESP_LOGW(TAG, "PHY [%s] skipped (no eth handle)", note);
        return;
    }

    esp_eth_phy_reg_rw_data_t rw = {
        .reg_addr = W5500_REG_PHYCFGR_SPI,
        .reg_value_p = &phycfg_raw,
    };
    if (esp_eth_ioctl(s_eth_handle, ETH_CMD_READ_PHY_REG, &rw) != ESP_OK) {
        ESP_LOGW(TAG, "PHY [%s] read failed", note);
        return;
    }

    const uint8_t phycfg = (uint8_t)(phycfg_raw & 0xFFU);
    const uint8_t link = (uint8_t)(phycfg & 1U);
    int int_lev = -1;
#if (BOARD_ETH_INT_GPIO >= 0)
    int_lev = gpio_get_level(BOARD_ETH_INT_GPIO);
#endif

    ESP_LOGI(TAG, "PHY [%s] PHYCFGR=0x%02x link=%u INT=%d",
             note, phycfg, (unsigned)link, int_lev);
}

static void RobinEth_PhyTimerCb(void *arg)
{
    (void)arg;
    RobinEth_LogPhy("1s");
}

static void eth_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_base;

    switch (event_id) {
    case ETHERNET_EVENT_CONNECTED: {
        esp_eth_handle_t eth_handle = *(esp_eth_handle_t *)event_data;
        uint8_t mac_addr[6] = {0};
        esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
        ESP_LOGI(TAG, "Ethernet link up, MAC %02x:%02x:%02x:%02x:%02x:%02x",
                 mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
        break;
    }
    case ETHERNET_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "Ethernet link down");
        break;
    case ETHERNET_EVENT_START:
        ESP_LOGI(TAG, "Ethernet started");
        break;
    case ETHERNET_EVENT_STOP:
        ESP_LOGI(TAG, "Ethernet stopped");
        break;
    default:
        break;
    }
}

static void got_ip_event_handler(void *arg, esp_event_base_t event_base,
                                 int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_base;
    (void)event_id;
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    const esp_netif_ip_info_t *ip_info = &event->ip_info;

    ESP_LOGI(TAG, "Ethernet got IP " IPSTR " gw " IPSTR, IP2STR(&ip_info->ip), IP2STR(&ip_info->gw));
}

static void RobinEth_ApplyStaticIp(esp_netif_t *netif)
{
    esp_netif_ip_info_t info = {0};
    uint8_t ip[4];
    uint8_t mask[4];
    uint8_t gw[4];

    if (netif == NULL) {
        return;
    }
    if (Params_GetEthDhcp() != 0) {
        ESP_LOGI(TAG, "Ethernet DHCP on (F7.4) — waiting for lease");
        return;
    }

    Params_GetEthIp(ip);
    Params_GetEthMask(mask);
    Params_GetEthGw(gw);

    IP4_ADDR(&info.ip, ip[0], ip[1], ip[2], ip[3]);
    IP4_ADDR(&info.netmask, mask[0], mask[1], mask[2], mask[3]);
    IP4_ADDR(&info.gw, gw[0], gw[1], gw[2], gw[3]);

    esp_err_t err = esp_netif_dhcpc_stop(netif);
    if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
        ESP_LOGW(TAG, "esp_netif_dhcpc_stop: %s", esp_err_to_name(err));
        return;
    }
    err = esp_netif_set_ip_info(netif, &info);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_netif_set_ip_info failed: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "Static IP " IPSTR " mask " IPSTR " gw " IPSTR,
             IP2STR(&info.ip), IP2STR(&info.netmask), IP2STR(&info.gw));
}

void RobinEth_Start(void)
{
    uint8_t eth_port_cnt = 0;
    esp_eth_handle_t *eth_handles = NULL;

    ESP_LOGI(TAG, "eth init (PHY log timer build Oct 9 2026)");
    ESP_LOGI(TAG, "F7.4 eth DHCP=%ld (0=static 192.168.1.100)", (long)Params_GetEthDhcp());

    {
        uint8_t w5500_ver = 0;
        esp_err_t probe = W5500_ProbeVersion(&w5500_ver);
        if (probe != ESP_OK) {
            ESP_LOGW(TAG, "W5500 SPI probe failed — fix PHY/pins before expecting RJ45 link");
        }
    }

    esp_err_t eth_init_err = example_eth_init(&eth_handles, &eth_port_cnt);
    if (eth_init_err != ESP_OK || eth_port_cnt == 0 || eth_handles == NULL) {
        ESP_LOGW(TAG, "Ethernet init failed (err=%s, ports=%u). Check sdkconfig W5500 + serial w5500_probe",
                 esp_err_to_name(eth_init_err), (unsigned)eth_port_cnt);
        return;
    }

    /* esp_eth may leave /RST as output; board has no external pull-up on IO46. */
    W5500_BoardRstRelease();

    RobinNet_Init();

    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
    s_eth_netif = esp_netif_new(&cfg);
    if (s_eth_netif == NULL) {
        ESP_LOGE(TAG, "esp_netif_new failed");
        return;
    }

    esp_eth_netif_glue_handle_t glue = esp_eth_new_netif_glue(eth_handles[0]);
    if (glue == NULL) {
        ESP_LOGE(TAG, "esp_eth_new_netif_glue failed");
        return;
    }

    ESP_ERROR_CHECK(esp_netif_attach(s_eth_netif, glue));
    RobinEth_ApplyStaticIp(s_eth_netif);

    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &got_ip_event_handler, NULL));

    for (int i = 0; i < eth_port_cnt; i++) {
        s_eth_handle = eth_handles[i];
        ESP_ERROR_CHECK(esp_eth_start(eth_handles[i]));
    }

    W5500_BoardRstRelease();

#if CONFIG_ACT1_ETH_PHY_MONITOR && !ACT1_SUPPRESS_BENCH_IO
    RobinEth_LogPhy("after-start");

    if (s_phy_timer == NULL) {
        const esp_timer_create_args_t phy_tmr = {
            .callback = RobinEth_PhyTimerCb,
            .name = "eth_phy",
        };
        if (esp_timer_create(&phy_tmr, &s_phy_timer) == ESP_OK) {
            (void)esp_timer_start_periodic(s_phy_timer, 1000000);
            ESP_LOGI(TAG, "PHY timer 1s (plug/unplug — watch link=0/1)");
        } else {
            ESP_LOGW(TAG, "PHY timer create failed");
        }
    }
#endif

#if CONFIG_EXAMPLE_USE_SPI_ETHERNET
    ESP_LOGI(TAG, "W5500 SPI host=%d CS=%d INT=%d RST=%d (shared SCLK/MOSI/MISO with ADC)",
             CONFIG_EXAMPLE_ETH_SPI_HOST, CONFIG_EXAMPLE_ETH_SPI_CS0_GPIO,
             CONFIG_EXAMPLE_ETH_SPI_INT0_GPIO, CONFIG_EXAMPLE_ETH_SPI_PHY_RST0_GPIO);
#endif
}
