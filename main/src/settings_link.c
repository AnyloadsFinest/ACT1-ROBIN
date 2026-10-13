#include "settings_link.h"

#include "settings_core.h"
#include "cJSON.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include <string.h>

static const char *TAG = "settings_link";

#define SL_PREFIX       "@ACT1"
#define SL_PREFIX_LEN   5
#define SL_LINE_MAX     4096

static int s_uart_num = -1;
static char s_line[SL_LINE_MAX];
static size_t s_line_len;

static void SettingsLink_ReplyOk(void)
{
    const char *msg = SL_PREFIX "{\"ok\":true}\n";
    if (s_uart_num >= 0) {
        uart_write_bytes(s_uart_num, msg, strlen(msg));
    }
}

static void SettingsLink_ReplyErr(const char *reason)
{
    char buf[128];
    int n = snprintf(buf, sizeof(buf), SL_PREFIX "{\"ok\":false,\"err\":\"%s\"}\n", reason ? reason : "error");
    if (n > 0 && s_uart_num >= 0) {
        uart_write_bytes(s_uart_num, buf, (size_t)n);
    }
}

static void SettingsLink_HandleLine(const char *json_start)
{
    cJSON *root = cJSON_Parse(json_start);
    cJSON *cmd;
    cJSON *settings;

    if (root == NULL) {
        SettingsLink_ReplyErr("json");
        return;
    }

    cmd = cJSON_GetObjectItem(root, "cmd");
    if (!cJSON_IsString(cmd) || cmd->valuestring == NULL) {
        cJSON_Delete(root);
        SettingsLink_ReplyErr("cmd");
        return;
    }

    if (strcmp(cmd->valuestring, "ping") == 0) {
        cJSON_Delete(root);
        SettingsLink_ReplyOk();
        return;
    }

    if (strcmp(cmd->valuestring, "apply") != 0) {
        cJSON_Delete(root);
        SettingsLink_ReplyErr("unknown_cmd");
        return;
    }

    settings = cJSON_GetObjectItem(root, "settings");
    if (!cJSON_IsObject(settings)) {
        cJSON_Delete(root);
        SettingsLink_ReplyErr("settings");
        return;
    }

    if (Settings_ApplyJsonObject(settings) != 0) {
        cJSON_Delete(root);
        SettingsLink_ReplyErr("nvs");
        return;
    }

    cJSON_Delete(root);
    SettingsLink_ReplyOk();
}

static void SettingsLink_FeedByte(uint8_t b)
{
    if (b == '\r') {
        return;
    }

    if (b == '\n') {
        if (s_line_len >= SL_PREFIX_LEN && strncmp(s_line, SL_PREFIX, SL_PREFIX_LEN) == 0) {
            s_line[s_line_len] = '\0';
            SettingsLink_HandleLine(s_line + SL_PREFIX_LEN);
        }
        s_line_len = 0;
        return;
    }

    if (s_line_len + 1U >= SL_LINE_MAX) {
        s_line_len = 0;
        return;
    }

    s_line[s_line_len++] = (char)b;
}

void SettingsLink_Init(void)
{
#if CONFIG_ESP_CONSOLE_UART_NUM >= 0
    s_uart_num = CONFIG_ESP_CONSOLE_UART_NUM;
#else
    s_uart_num = UART_NUM_0;
#endif
    s_line_len = 0;
    ESP_LOGI(TAG, "UART settings link optional (%s)", SL_PREFIX);
}

void SettingsLink_Tick(void)
{
    uint8_t buf[64];
    int n;
    int i;

    if (s_uart_num < 0) {
        return;
    }

    n = uart_read_bytes(s_uart_num, buf, sizeof(buf), 0);
    if (n <= 0) {
        return;
    }

    for (i = 0; i < n; i++) {
        SettingsLink_FeedByte(buf[i]);
    }
}
