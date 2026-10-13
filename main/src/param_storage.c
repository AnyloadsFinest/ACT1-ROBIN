#include "param_storage.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <string.h>

#define NVS_NS   "act1"
#define NVS_KEY  "params"

static const char *TAG = "param_store";

static ParamStoreRecord s_ram_store;
static int s_ram_valid = 0;
static int s_nvs_ready = 0;

static uint32_t ParamStorage_Crc32Update(uint32_t crc, const uint8_t *data, uint32_t len)
{
    uint32_t i;
    uint8_t  bit;

    for (i = 0U; i < len; i++)
    {
        crc ^= data[i];
        for (bit = 0U; bit < 8U; bit++)
        {
            if (crc & 1U)
            {
                crc = (crc >> 1U) ^ 0xEDB88320UL;
            }
            else
            {
                crc >>= 1U;
            }
        }
    }

    return crc;
}

static int ParamStorage_RecordIsValid(const ParamStoreRecord *rec)
{
    if (rec == NULL)
    {
        return 0;
    }

    if (rec->magic != PARAM_STORE_MAGIC)
    {
        return 0;
    }

    if (rec->version != PARAM_STORE_VERSION)
    {
        return 0;
    }

    if (rec->payload_len != (uint16_t)sizeof(ParamStorePayload))
    {
        return 0;
    }

    if (rec->crc32 != ParamStorage_CalcCrc(rec))
    {
        return 0;
    }

    return 1;
}

uint32_t ParamStorage_CalcCrc(const ParamStoreRecord *rec)
{
    return ParamStorage_Crc32Update(0xFFFFFFFFUL,
                                    (const uint8_t *)&rec->payload,
                                    (uint32_t)sizeof(ParamStorePayload)) ^ 0xFFFFFFFFUL;
}

int ParamStorage_Init(void)
{
    esp_err_t err = nvs_flash_init();

    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        err = nvs_flash_erase();
        if (err == ESP_OK)
        {
            err = nvs_flash_init();
        }
    }

    s_nvs_ready = (err == ESP_OK) ? 1 : 0;
    if (s_nvs_ready == 0)
    {
        ESP_LOGE(TAG, "nvs init failed (%s)", esp_err_to_name(err));
        return -1;
    }

    return 0;
}

int ParamStorage_Read(ParamStoreRecord *out)
{
    nvs_handle_t handle;
    ParamStoreRecord rec;
    size_t len;
    esp_err_t err;

    if (out == NULL)
    {
        return -1;
    }

    if (s_ram_valid != 0)
    {
        memcpy(out, &s_ram_store, sizeof(ParamStoreRecord));
        return 0;
    }

    if (s_nvs_ready == 0)
    {
        return -1;
    }

    err = nvs_open(NVS_NS, NVS_READONLY, &handle);
    if (err != ESP_OK)
    {
        return -1;
    }

    len = sizeof(rec);
    err = nvs_get_blob(handle, NVS_KEY, &rec, &len);
    nvs_close(handle);
    if (err != ESP_OK || len != sizeof(rec) || ParamStorage_RecordIsValid(&rec) == 0)
    {
        return -1;
    }

    s_ram_store = rec;
    s_ram_valid = 1;
    memcpy(out, &rec, sizeof(ParamStoreRecord));
    return 0;
}

int ParamStorage_Write(const ParamStoreRecord *in)
{
    ParamStoreRecord rec;
    nvs_handle_t handle;
    esp_err_t err;

    if (in == NULL)
    {
        return -1;
    }

    rec = *in;
    rec.magic = PARAM_STORE_MAGIC;
    rec.version = PARAM_STORE_VERSION;
    rec.payload_len = (uint16_t)sizeof(ParamStorePayload);
    rec.crc32 = ParamStorage_CalcCrc(&rec);
    s_ram_store = rec;
    s_ram_valid = 1;

    if (s_nvs_ready == 0)
    {
        return -1;
    }

    err = nvs_open(NVS_NS, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "nvs open failed (%s)", esp_err_to_name(err));
        return -1;
    }

    err = nvs_set_blob(handle, NVS_KEY, &rec, sizeof(rec));
    if (err == ESP_OK)
    {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "nvs write failed (%s)", esp_err_to_name(err));
        return -1;
    }

    return 0;
}
