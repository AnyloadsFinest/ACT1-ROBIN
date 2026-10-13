#include "display.h"
#include "act1_ui.h"
#include "params.h"
#include "scale.h"
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"

#define OLED_ADDR_7BIT  0x3C
#define OLED_WIDTH      128
#define OLED_HEIGHT     64
#define OLED_I2C_TO_MS  100

static i2c_master_dev_handle_t display_dev;
static uint8_t oled_buffer[OLED_WIDTH * OLED_HEIGHT / 8];

static uint8_t cursor_x = 0;
static uint8_t cursor_y = 0;

static void HAL_Delay(uint32_t ms)
{
    while (ms > 0U) {
        uint32_t chunk = (ms > 10U) ? 10U : ms;

        vTaskDelay(pdMS_TO_TICKS(chunk));
        Act1_BootYield();
        ms -= chunk;
    }
}

static void OLED_WriteCommand(uint8_t cmd)
{
    uint8_t data[2];

    data[0] = 0x00;
    data[1] = cmd;

    if (display_dev != NULL) {
        (void)i2c_master_transmit(display_dev, data, 2, OLED_I2C_TO_MS);
    }
}

static void OLED_DrawPixel(uint8_t x, uint8_t y, uint8_t color)
{
    if (x >= OLED_WIDTH || y >= OLED_HEIGHT)
    {
        return;
    }

    if (color)
    {
        oled_buffer[x + (y / 8) * OLED_WIDTH] |= (1 << (y % 8));
    }
    else
    {
        oled_buffer[x + (y / 8) * OLED_WIDTH] &= ~(1 << (y % 8));
    }
}

static void OLED_FillRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t color)
{
    uint8_t dx;
    uint8_t dy;

    for (dy = 0; dy < h; dy++)
    {
        for (dx = 0; dx < w; dx++)
        {
            OLED_DrawPixel(x + dx, y + dy, color);
        }
    }
}

static void Display_FormatWeightInternal(int32_t weight, uint8_t decimal_pt, char *out, uint8_t out_size)
{
    int32_t abs_weight;
    int32_t divisor;
    int32_t whole;
    int32_t frac;
    uint8_t dp;
    uint8_t i;

    if (out_size == 0U)
    {
        return;
    }

    dp = decimal_pt;
    if (dp > 4U)
    {
        dp = 4U;
    }

    divisor = 1;
    for (i = 0U; i < dp; i++)
    {
        divisor *= 10;
    }

    if (dp == 0U)
    {
        snprintf(out, out_size, "%ld", (long)weight);
        return;
    }

    if (weight < 0)
    {
        abs_weight = -weight;
        whole = abs_weight / divisor;
        frac = abs_weight % divisor;
        snprintf(out, out_size, "-%ld.%0*ld", (long)whole, (int)dp, (long)frac);
    }
    else
    {
        whole = weight / divisor;
        frac = weight % divisor;
        snprintf(out, out_size, "%ld.%0*ld", (long)whole, (int)dp, (long)frac);
    }
}

void Display_FormatWeight(int32_t weight, uint8_t decimal_pt, char *out, uint8_t out_size)
{
    Display_FormatWeightInternal(weight, decimal_pt, out, out_size);
}

static uint8_t Display_WeightCharAdvance(uint8_t scale_x, uint8_t extra_space)
{
    return (uint8_t)((5U * scale_x) + extra_space);
}

static uint8_t Display_WeightTextWidthPx(uint8_t len, uint8_t scale_x, uint8_t extra_space)
{
    return (uint8_t)(len * Display_WeightCharAdvance(scale_x, extra_space));
}

static void Display_PickWeightFont(uint8_t len, uint8_t *scale_x, uint8_t *scale_y, uint8_t *extra_space)
{
    static const struct
    {
        uint8_t sx;
        uint8_t sy;
        uint8_t ex;
    } tiers[] = {
        { 3U, 4U, 5U },
        { 3U, 3U, 3U },
        { 2U, 4U, 4U },
        { 2U, 3U, 3U },
        { 2U, 2U, 2U },
        { 2U, 2U, 1U },
        { 1U, 2U, 2U },
        { 1U, 2U, 1U },
    };
    uint8_t i;
    const uint8_t max_px = (OLED_WIDTH > 4U) ? (uint8_t)(OLED_WIDTH - 4U) : OLED_WIDTH;

    for (i = 0U; i < (uint8_t)(sizeof(tiers) / sizeof(tiers[0])); i++)
    {
        if (Display_WeightTextWidthPx(len, tiers[i].sx, tiers[i].ex) <= max_px)
        {
            *scale_x = tiers[i].sx;
            *scale_y = tiers[i].sy;
            *extra_space = tiers[i].ex;
            return;
        }
    }

    *scale_x = 1U;
    *scale_y = 2U;
    *extra_space = 1U;
}

static uint8_t Display_WeightBaselineY(uint8_t scale_y)
{
    /* Vertically center ~7-row glyphs in the upper weight band (above status line). */
    uint8_t glyph_h = (uint8_t)(7U * scale_y);

    if (glyph_h >= 46U)
    {
        return 18U;
    }
    return (uint8_t)((46U - glyph_h) / 2U);
}

void Display_ShowWeightFormatted(int32_t weight, uint8_t decimal_pt, const char *units)
{
    char weight_str[20];
    uint8_t len;
    uint8_t x;
    uint8_t scale_x;
    uint8_t scale_y;
    uint8_t extra_space;
    uint8_t text_w;
    uint8_t y;
    uint8_t dp = decimal_pt;
    const char *unit_label = (units != NULL) ? units : "KG";

    if (dp > 4U)
    {
        dp = 4U;
    }

    Display_FormatWeightInternal(weight, dp, weight_str, sizeof(weight_str));
    len = (uint8_t)strlen(weight_str);

    while (dp > 0U && len > 8U &&
           Display_WeightTextWidthPx(len, 1U, 1U) > (OLED_WIDTH - 4U))
    {
        dp--;
        Display_FormatWeightInternal(weight, dp, weight_str, sizeof(weight_str));
        len = (uint8_t)strlen(weight_str);
    }

    Display_PickWeightFont(len, &scale_x, &scale_y, &extra_space);
    text_w = Display_WeightTextWidthPx(len, scale_x, extra_space);
    x = (text_w >= OLED_WIDTH) ? 0U : (uint8_t)((OLED_WIDTH - text_w) / 2U);
    y = Display_WeightBaselineY(scale_y);

    Display_Clear();

    if (Scale_IsOverload())
    {
        uint8_t ol_len = 6U;

        Display_PickWeightFont(ol_len, &scale_x, &scale_y, &extra_space);
        text_w = Display_WeightTextWidthPx(ol_len, scale_x, extra_space);
        x = (text_w >= OLED_WIDTH) ? 0U : (uint8_t)((OLED_WIDTH - text_w) / 2U);
        y = Display_WeightBaselineY(scale_y);
        Display_SetCursor(x, y);
        Display_WriteStringScaled("------", scale_x, scale_y, extra_space);
    }
    else
    {
        Display_SetCursor(x, y);
        Display_WriteStringScaled(weight_str, scale_x, scale_y, extra_space);
    }

    if (Scale_IsAtZero())
    {
        Display_DrawRect(0, 53, 9, 10, 1);
        Display_SetCursor(2, 55);
        Display_WriteStringBig("0", 1);
    }

    Display_SetCursor(16, 55);
    Display_WriteStringBig(Scale_IsStable() ? "STB" : "MOT", 1);

    Display_SetCursor(48, 55);
    Display_WriteStringBig(Scale_IsNet() ? "NT" : "GR", 1);

    Display_SetCursor(116, 55);
    Display_WriteStringBig(unit_label, 1);

    Display_Update();
}

void Display_ShowWeight(int32_t weight)
{
    Display_ShowWeightFormatted(weight, (uint8_t)Params_GetDecimalPtFor(weight), Params_GetUnitsLabel());
}


static const uint8_t *OLED_GetGlyph(char c)
{
    static const uint8_t blank[5] = {0x00,0x00,0x00,0x00,0x00};

    static const uint8_t A[5] = {0x7E,0x11,0x11,0x11,0x7E};
    static const uint8_t D[5] = {0x7F,0x41,0x41,0x22,0x1C};
    static const uint8_t L[5] = {0x7F,0x40,0x40,0x40,0x40};
    static const uint8_t N[5] = {0x7F,0x02,0x04,0x08,0x7F};
    static const uint8_t O[5] = {0x3E,0x41,0x41,0x41,0x3E};
    static const uint8_t Y[5] = {0x07,0x08,0x70,0x08,0x07};

    static const uint8_t n0[5] = {0x3E,0x51,0x49,0x45,0x3E};
    static const uint8_t n1[5] = {0x00,0x42,0x7F,0x40,0x00};
    static const uint8_t n2[5] = {0x42,0x61,0x51,0x49,0x46};
    static const uint8_t n3[5] = {0x21,0x41,0x45,0x4B,0x31};
    static const uint8_t n4[5] = {0x18,0x14,0x12,0x7F,0x10};
    static const uint8_t n5[5] = {0x27,0x45,0x45,0x45,0x39};
    static const uint8_t n6[5] = {0x3C,0x4A,0x49,0x49,0x30};
    static const uint8_t n7[5] = {0x01,0x71,0x09,0x05,0x03};
    static const uint8_t n8[5] = {0x36,0x49,0x49,0x49,0x36};
    static const uint8_t n9[5] = {0x06,0x49,0x49,0x29,0x1E};
    static const uint8_t B[5] = {0x7F,0x49,0x49,0x49,0x36};
    static const uint8_t C[5] = {0x3E,0x41,0x41,0x41,0x41};
    static const uint8_t E[5] = {0x7F,0x49,0x49,0x49,0x41};
    static const uint8_t F[5] = {0x7F,0x09,0x09,0x09,0x01};
    static const uint8_t H[5] = {0x7F,0x08,0x08,0x08,0x7F};
    static const uint8_t I[5] = {0x41,0x41,0x7F,0x41,0x41};
    static const uint8_t J[5] = {0x20,0x40,0x41,0x3F,0x01};
    static const uint8_t M[5] = {0x7F,0x02,0x0C,0x02,0x7F};
    static const uint8_t P[5] = {0x7F,0x09,0x09,0x09,0x06};
    static const uint8_t Q[5] = {0x3E,0x41,0x51,0x21,0x5E};
    static const uint8_t R[5] = {0x7F,0x09,0x19,0x29,0x46};
    static const uint8_t S[5] = {0x46,0x49,0x49,0x49,0x31};
    static const uint8_t T[5] = {0x01,0x01,0x7F,0x01,0x01};
    static const uint8_t U[5] = {0x3F,0x40,0x40,0x40,0x3F};
    static const uint8_t V[5] = {0x0F,0x30,0x40,0x30,0x0F};
    static const uint8_t W[5] = {0x7F,0x20,0x18,0x20,0x7F};
    static const uint8_t X[5] = {0x41,0x22,0x1C,0x22,0x41};
    static const uint8_t Z[5] = {0x61,0x51,0x49,0x45,0x43};
    static const uint8_t space[5]   = {0x00,0x00,0x00,0x00,0x00};
    static const uint8_t dash[5]    = {0x08,0x08,0x08,0x08,0x08};
    static const uint8_t dot[5]     = {0x00,0x20,0x00,0x00,0x00};
    static const uint8_t pct[5]     = {0x63,0x10,0x08,0x26,0x41};
    static const uint8_t arrow_l[5] = {0x08,0x14,0x22,0x41,0x00};
    static const uint8_t arrow_r[5] = {0x00,0x41,0x22,0x14,0x08};
    static const uint8_t arrow_u[5] = {0x04,0x02,0x01,0x02,0x04};
    static const uint8_t arrow_d[5] = {0x10,0x20,0x40,0x20,0x10};

    static const uint8_t K[5] = {0x7F,0x08,0x14,0x22,0x41};
    static const uint8_t G[5] = {0x3E,0x41,0x49,0x49,0x7A};

    static const uint8_t k[5]  = {0x7F,0x08,0x14,0x22,0x40};
    static const uint8_t g[5]  = {0x38,0x44,0x44,0x44,0x7C};

    switch (c)
    {
        case 'A': return A;
        case 'D': return D;
        case 'L': return L;
        case 'N': return N;
        case 'O': return O;
        case 'Y': return Y;

        case '0': return n0;
        case '1': return n1;
        case '2': return n2;
        case '3': return n3;
        case '4': return n4;
        case '5': return n5;
        case '6': return n6;
        case '7': return n7;
        case '8': return n8;
        case '9': return n9;

        case 'k': return k;
        case 'g': return g;
        case 'K': return K;
        case 'G': return G;
        case 'B': return B;
        case 'C': return C;
        case 'E': return E;
        case 'F': return F;
        case 'H': return H;
        case 'I': return I;
        case 'J': return J;
        case 'M': return M;
        case 'P': return P;
        case 'Q': return Q;
        case 'R': return R;
        case 'S': return S;
        case 'T': return T;
        case 'U': return U;
        case 'V': return V;
        case 'W': return W;
        case 'X': return X;
        case 'Z': return Z;
        case ' ': return space;
        case '-': return dash;
        case '.': return dot;
        case '%': return pct;
        case '<': return arrow_l;
        case '>': return arrow_r;
        case '^': return arrow_u;
        case 'v': return arrow_d;
        default:  return blank;
    }
}

void Display_Init(i2c_master_bus_handle_t bus)
{
    uint8_t addr = OLED_ADDR_7BIT;
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 400000,
    };

    HAL_Delay(100);

    if (i2c_master_probe(bus, 0x3C, 50) != ESP_OK) {
        if (i2c_master_probe(bus, 0x3D, 50) == ESP_OK) {
            addr = 0x3D;
            dev_cfg.device_address = addr;
        }
    }

    if (i2c_master_bus_add_device(bus, &dev_cfg, &display_dev) != ESP_OK) {
        ESP_LOGE("display", "OLED I2C device add failed");
        display_dev = NULL;
        return;
    }

    ESP_LOGI("display", "OLED at 0x%02X", addr);

    OLED_WriteCommand(0xAE); /* display off */
    OLED_WriteCommand(0x20); /* memory addressing mode */
    OLED_WriteCommand(0x00); /* horizontal addressing mode */
    OLED_WriteCommand(0xB0); /* page start address */

    OLED_WriteCommand(0xC0); /* flip up/down */
    OLED_WriteCommand(0x00); /* low column */
    OLED_WriteCommand(0x10); /* high column */
    OLED_WriteCommand(0x40); /* start line */
    OLED_WriteCommand(0x81); /* contrast */
    OLED_WriteCommand(0x7F);

    OLED_WriteCommand(0xA1); /* keep left/right same */
    OLED_WriteCommand(0xA6); /* normal display */
    OLED_WriteCommand(0xA8); /* multiplex ratio */
    OLED_WriteCommand(0x3F);
    OLED_WriteCommand(0xA4); /* display follows RAM */
    OLED_WriteCommand(0xD3); /* display offset */
    OLED_WriteCommand(0x00);
    OLED_WriteCommand(0xD5); /* display clock */
    OLED_WriteCommand(0x80);
    OLED_WriteCommand(0xD9); /* pre-charge */
    OLED_WriteCommand(0xF1);
    OLED_WriteCommand(0xDA); /* COM pins */
    OLED_WriteCommand(0x12);
    OLED_WriteCommand(0xDB); /* VCOMH */
    OLED_WriteCommand(0x40);
    OLED_WriteCommand(0x8D); /* charge pump */
    OLED_WriteCommand(0x14);
    OLED_WriteCommand(0xAF); /* display on */

    Display_Clear();
    Display_Update();
}

void Display_Clear(void)
{
    memset(oled_buffer, 0x00, sizeof(oled_buffer));
    cursor_x = 0;
    cursor_y = 0;
}

void Display_Update(void)
{
    uint8_t data[OLED_WIDTH + 1];

    data[0] = 0x40;

    for (uint8_t page = 0; page < 8; page++)
    {
        OLED_WriteCommand(0xB0 + page);
        OLED_WriteCommand(0x00);
        OLED_WriteCommand(0x10);

        memcpy(&data[1], &oled_buffer[OLED_WIDTH * page], OLED_WIDTH);
        if (display_dev != NULL) {
            (void)i2c_master_transmit(display_dev, data, OLED_WIDTH + 1, OLED_I2C_TO_MS);
        }
    }
}

void Display_SetCursor(uint8_t x, uint8_t y)
{
    cursor_x = x;
    cursor_y = y;
}

void Display_WriteStringBig(const char *str, uint8_t scale)
{
    const uint8_t *glyph;
    uint8_t col;
    uint8_t row;
    uint8_t sx;
    uint8_t sy;
    uint8_t line;
    uint8_t base_x;
    uint8_t base_y;

    while (*str)
    {
        glyph = OLED_GetGlyph(*str);
        base_x = cursor_x;
        base_y = cursor_y;

        for (col = 0; col < 5; col++)
        {
            line = glyph[col];

            for (row = 0; row < 7; row++)
            {
                if (line & (1 << row))
                {
                    for (sx = 0; sx < scale; sx++)
                    {
                        for (sy = 0; sy < scale; sy++)
                        {
                            OLED_DrawPixel(base_x + col * scale + sx,
                                           base_y + row * scale + sy,
                                           1);
                        }
                    }
                }
            }
        }

        cursor_x += 6 * scale;
        str++;
    }
}

void Display_ShowAnyload(void)
{
    Display_Clear();

    Display_SetCursor(2, 20);
    Display_WriteStringBig("ANYLOAD", 3);

    Display_Update();
}


void Display_WriteStringBigTight(const char *str, uint8_t scale)
{
    const uint8_t *glyph;
    uint8_t col;
    uint8_t row;
    uint8_t sx;
    uint8_t sy;
    uint8_t line;
    uint8_t base_x;
    uint8_t base_y;

    while (*str)
    {
        glyph = OLED_GetGlyph(*str);
        base_x = cursor_x;
        base_y = cursor_y;

        for (col = 0; col < 5; col++)
        {
            line = glyph[col];

            for (row = 0; row < 7; row++)
            {
                if (line & (1 << row))
                {
                    for (sx = 0; sx < scale; sx++)
                    {
                        for (sy = 0; sy < scale; sy++)
                        {
                            OLED_DrawPixel(base_x + col * scale + sx,
                                           base_y + row * scale + sy,
                                           1);
                        }
                    }
                }
            }
        }

        cursor_x += 5 * scale;   /* tighter spacing than 6*scale */
        str++;
    }
}


void Display_WriteStringScaled(const char *str, uint8_t scale_x, uint8_t scale_y, uint8_t extra_space)
{
    Display_WriteStringScaledColor(str, scale_x, scale_y, extra_space, 1U);
}

void Display_WriteStringScaledColor(const char *str, uint8_t scale_x, uint8_t scale_y, uint8_t extra_space, uint8_t color)
{
    const uint8_t *glyph;
    uint8_t col;
    uint8_t row;
    uint8_t sx;
    uint8_t sy;
    uint8_t line;
    uint8_t base_x;
    uint8_t base_y;

    while (*str)
    {
        glyph = OLED_GetGlyph(*str);
        base_x = cursor_x;
        base_y = cursor_y;

        for (col = 0; col < 5; col++)
        {
            line = glyph[col];

            for (row = 0; row < 7; row++)
            {
                if (line & (1 << row))
                {
                    for (sx = 0; sx < scale_x; sx++)
                    {
                        for (sy = 0; sy < scale_y; sy++)
                        {
                            OLED_DrawPixel(base_x + col * scale_x + sx,
                                           base_y + row * scale_y + sy,
                                           color);
                        }
                    }
                }
            }
        }

        cursor_x += (5 * scale_x) + extra_space;
        str++;
    }
}

void Display_FillRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t color)
{
    OLED_FillRect(x, y, w, h, color);
}

void Display_DrawRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t color)
{
    if (w < 2U || h < 2U)
    {
        return;
    }

    OLED_FillRect(x, y, w, 1U, color);
    OLED_FillRect(x, (uint8_t)(y + h - 1U), w, 1U, color);
    OLED_FillRect(x, y, 1U, h, color);
    OLED_FillRect((uint8_t)(x + w - 1U), y, 1U, h, color);
}

void Display_DrawProgressBar(uint8_t x, uint8_t y, uint8_t width, uint8_t height, uint8_t percent)
{
    uint8_t fill_w;

    if (width < 2U || height < 2U)
    {
        return;
    }

    if (percent > 100U)
    {
        percent = 100U;
    }

    OLED_FillRect(x, y, width, height, 0);
    OLED_FillRect(x, y, width, 1, 1);
    OLED_FillRect(x, y + height - 1U, width, 1, 1);
    OLED_FillRect(x, y, 1, height, 1);
    OLED_FillRect(x + width - 1U, y, 1, height, 1);

    fill_w = (uint8_t)(((uint16_t)(width - 2U) * percent) / 100U);
    if (fill_w > 0U)
    {
        OLED_FillRect(x + 1U, y + 1U, fill_w, height - 2U, 1);
    }
}

static uint8_t Boot_TextWidth(const char *str, uint8_t count, uint8_t scale, uint8_t extra)
{
    return (uint8_t)(count * ((5U * scale) + extra));
}

static void Boot_DrawLine(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, uint8_t color)
{
    int16_t x = (int16_t)x0;
    int16_t y = (int16_t)y0;
    int16_t dx;
    int16_t dy;
    int16_t sx;
    int16_t sy;
    int16_t err;
    int16_t e2;

    dx = (x1 > x0) ? (int16_t)(x1 - x0) : (int16_t)(x0 - x1);
    dy = (y1 > y0) ? (int16_t)(y1 - y0) : (int16_t)(y0 - y1);
    sx = (x0 < x1) ? 1 : -1;
    sy = (y0 < y1) ? 1 : -1;
    err = dx - dy;

    for (;;)
    {
        if (x >= 0 && y >= 0 && x < OLED_WIDTH && y < OLED_HEIGHT)
        {
            OLED_DrawPixel((uint8_t)x, (uint8_t)y, color);
        }

        if (x == (int16_t)x1 && y == (int16_t)y1)
        {
            break;
        }

        e2 = err * 2;
        if (e2 > -dy)
        {
            err -= dy;
            x += sx;
        }

        if (e2 < dx)
        {
            err += dx;
            y += sy;
        }
    }
}

static void Boot_DrawScaleHardware(void)
{
    uint8_t base_x = 42U;
    uint8_t base_y = 44U;
    uint8_t base_w = 44U;
    uint8_t base_h = 5U;

    OLED_FillRect((uint8_t)(base_x + 2U), (uint8_t)(base_y - 1U), (uint8_t)(base_w - 4U), 1U, 1U);
    OLED_FillRect(base_x, base_y, base_w, base_h, 1U);
    OLED_FillRect((uint8_t)(base_x + 1U), (uint8_t)(base_y + 1U), (uint8_t)(base_w - 2U), (uint8_t)(base_h - 2U), 0U);
    OLED_FillRect((uint8_t)(base_x + 4U), (uint8_t)(base_y + 2U), (uint8_t)(base_w - 8U), 1U, 1U);

    OLED_FillRect((uint8_t)(base_x - 2U), (uint8_t)(base_y + base_h), 7U, 2U, 1U);
    OLED_FillRect((uint8_t)(base_x + base_w - 5U), (uint8_t)(base_y + base_h), 7U, 2U, 1U);
    OLED_FillRect((uint8_t)(base_x + base_w - 10U), (uint8_t)(base_y + 2U), 3U, 2U, 1U);

    OLED_FillRect(46U, 39U, 5U, 5U, 1U);
    OLED_FillRect(77U, 39U, 5U, 5U, 1U);
    OLED_FillRect(47U, 38U, 3U, 1U, 1U);
    OLED_FillRect(78U, 38U, 3U, 1U, 1U);

    OLED_FillRect(34U, 33U, 60U, 2U, 1U);
    OLED_FillRect(36U, 35U, 56U, 2U, 1U);
    OLED_FillRect(32U, 37U, 64U, 1U, 1U);
    Boot_DrawLine(34U, 33U, 36U, 35U, 1U);
    Boot_DrawLine(94U, 33U, 92U, 35U, 1U);
    Boot_DrawLine(32U, 37U, 34U, 35U, 1U);
    Boot_DrawLine(96U, 37U, 92U, 35U, 1U);

    OLED_DrawPixel(38U, 34U, 1U);
    OLED_DrawPixel(90U, 34U, 1U);
}

static void Boot_DrawLoadItem(uint8_t progress)
{
    uint8_t plat_top = 33U;
    uint8_t crate_x = 52U;
    uint8_t crate_w = 18U;
    uint8_t crate_h;
    uint8_t crate_y;
    uint8_t side_x;

    if (progress == 0U)
    {
        return;
    }

    if (progress < 25U)
    {
        uint8_t pallet_w = (uint8_t)(8U + ((progress * 14U) / 25U));
        uint8_t pallet_x = (uint8_t)(64U - (pallet_w / 2U));

        OLED_FillRect((uint8_t)(pallet_x - 1U), (uint8_t)(plat_top - 5U), (uint8_t)(pallet_w + 2U), 6U, 1U);
        OLED_FillRect(pallet_x, (uint8_t)(plat_top - 4U), 3U, 2U, 0U);
        OLED_FillRect((uint8_t)(pallet_x + pallet_w - 5U), (uint8_t)(plat_top - 4U), 3U, 2U, 0U);
        OLED_FillRect((uint8_t)(pallet_x + (pallet_w / 2U) - 1U), (uint8_t)(plat_top - 4U), 2U, 2U, 0U);
        return;
    }

    OLED_FillRect(46U, (uint8_t)(plat_top - 2U), 36U, 2U, 1U);
    OLED_FillRect(48U, (uint8_t)(plat_top - 4U), 4U, 2U, 1U);
    OLED_FillRect(76U, (uint8_t)(plat_top - 4U), 4U, 2U, 1U);
    OLED_FillRect(62U, (uint8_t)(plat_top - 4U), 4U, 2U, 1U);
    Boot_DrawLine(48U, (uint8_t)(plat_top - 2U), 52U, (uint8_t)(plat_top - 4U), 1U);
    Boot_DrawLine(80U, (uint8_t)(plat_top - 2U), 76U, (uint8_t)(plat_top - 4U), 1U);

    crate_h = (uint8_t)(4U + (((progress - 25U) * 12U) / 75U));
    if (crate_h > 16U)
    {
        crate_h = 16U;
    }

    crate_y = (uint8_t)(plat_top - 2U - crate_h);
    side_x = (uint8_t)(crate_x + crate_w);

    OLED_FillRect(crate_x, crate_y, crate_w, crate_h, 1U);
    Display_DrawRect(crate_x, crate_y, crate_w, crate_h, 1U);

    if (crate_h >= 4U)
    {
        OLED_FillRect((uint8_t)(crate_x + 1U), (uint8_t)(crate_y + (crate_h / 2U)), (uint8_t)(crate_w - 2U), 1U, 0U);
        OLED_FillRect((uint8_t)(crate_x + (crate_w / 2U) - 1U), (uint8_t)(crate_y + 1U), 2U, (uint8_t)(crate_h - 2U), 0U);
        Boot_DrawLine((uint8_t)(crate_x + 2U), (uint8_t)(crate_y + 2U),
                      (uint8_t)(crate_x + crate_w - 3U), (uint8_t)(crate_y + crate_h - 3U), 0U);
        Boot_DrawLine((uint8_t)(crate_x + crate_w - 3U), (uint8_t)(crate_y + 2U),
                      (uint8_t)(crate_x + 2U), (uint8_t)(crate_y + crate_h - 3U), 0U);
    }

    if (crate_h >= 7U)
    {
        uint8_t top_y = (uint8_t)(crate_y - 3U);
        uint8_t side_w = 5U;

        OLED_FillRect((uint8_t)(crate_x + 4U), top_y, crate_w, 3U, 1U);
        OLED_FillRect((uint8_t)(side_x + 1U), (uint8_t)(crate_y - 1U), (uint8_t)(side_w - 1U), crate_h, 1U);

        Boot_DrawLine(crate_x, crate_y, (uint8_t)(crate_x + 4U), top_y, 1U);
        Boot_DrawLine((uint8_t)(crate_x + crate_w), crate_y, (uint8_t)(crate_x + crate_w + 4U), top_y, 1U);
        Boot_DrawLine((uint8_t)(crate_x + 4U), top_y, (uint8_t)(crate_x + crate_w + 4U), top_y, 1U);
        Boot_DrawLine((uint8_t)(crate_x + crate_w + 4U), top_y, side_x, crate_y, 1U);
        Boot_DrawLine(side_x, crate_y, (uint8_t)(side_x + side_w), (uint8_t)(crate_y - 2U), 1U);
        Boot_DrawLine((uint8_t)(side_x + side_w), (uint8_t)(crate_y - 2U),
                      (uint8_t)(side_x + side_w), (uint8_t)(crate_y + crate_h - 2U), 1U);
        Boot_DrawLine((uint8_t)(side_x + side_w), (uint8_t)(crate_y + crate_h - 2U),
                      side_x, (uint8_t)(crate_y + crate_h), 1U);
        Boot_DrawLine(side_x, (uint8_t)(crate_y + crate_h), side_x, crate_y, 1U);
        Boot_DrawLine((uint8_t)(side_x + 2U), (uint8_t)(crate_y + 4U),
                      (uint8_t)(side_x + 2U), (uint8_t)(crate_y + crate_h - 4U), 0U);
    }

    if (progress >= 90U)
    {
        OLED_DrawPixel((uint8_t)(crate_x + 3U), (uint8_t)(crate_y + 2U), 0U);
        OLED_DrawPixel((uint8_t)(crate_x + crate_w - 4U), (uint8_t)(crate_y + 2U), 0U);
    }
}

static void Boot_DrawScaleScene(uint8_t progress)
{
    Boot_DrawScaleHardware();
    Boot_DrawLoadItem(progress);
}

static void Boot_DrawLogo(uint8_t count, uint8_t y, uint8_t slide)
{
    static const char logo[] = "ANYLOAD";
    uint8_t scale = 2U;
    uint8_t extra = 2U;
    uint8_t x;
    uint8_t i;
    char letter[2];

    if (count == 0U)
    {
        return;
    }

    if (count > 7U)
    {
        count = 7U;
    }

    x = (uint8_t)((OLED_WIDTH - Boot_TextWidth(logo, count, scale, extra)) / 2U);
    letter[1] = '\0';

    for (i = 0U; i < count; i++)
    {
        uint8_t cy = y;

        letter[0] = logo[i];
        if ((i + 1U) == count && slide > 0U)
        {
            cy = (uint8_t)(y + slide);
        }

        Display_SetCursor(x, cy);
        Display_WriteStringScaled(letter, scale, scale, extra);
        x = (uint8_t)(x + (5U * scale) + extra);
    }
}

static void Boot_DrawLogoFrame(uint8_t count, uint8_t slide, uint8_t underline_w, uint8_t progress)
{
    uint8_t scale = 2U;
    uint8_t extra = 2U;
    uint8_t logo_y = 12U;
    uint8_t logo_w;
    uint8_t logo_x;

    Display_Clear();

    if (progress == 0U)
    {
        Boot_DrawLogo(count, logo_y, slide);

        if (underline_w > 0U && count >= 7U)
        {
            logo_w = Boot_TextWidth("ANYLOAD", 7U, scale, extra);
            logo_x = (uint8_t)((OLED_WIDTH - logo_w) / 2U);
            OLED_FillRect(logo_x, 29U, underline_w, 2U, 1U);
        }
    }

    if (count >= 7U)
    {
        Boot_DrawScaleScene(progress);
        Display_DrawProgressBar(16U, 50U, 96U, 5U, progress);
    }

    Display_Update();
}

typedef struct
{
    uint8_t x0;
    uint8_t y0;
    uint8_t phase;
    int8_t  dx;
    int8_t  dy;
    uint8_t big;
} BootSparkle;

static const BootSparkle boot_sparkles[] =
{
    { 14U,  9U, 0U,  1,  1, 0U },
    { 112U, 11U, 1U, -1,  1, 0U },
    {  64U,  5U, 2U,  0,  1, 1U },
    {  34U, 18U, 3U,  1,  0, 0U },
    {  94U, 16U, 0U, -1,  0, 0U },
    {  10U, 34U, 1U,  1, -1, 0U },
    { 116U, 32U, 2U, -1, -1, 0U },
    {  46U,  8U, 3U,  0,  1, 0U },
    {  82U,  7U, 0U,  1,  1, 1U },
    {  22U, 44U, 1U,  1,  0, 0U },
    { 104U, 42U, 2U, -1,  0, 0U },
    {  64U, 22U, 3U,  0, -1, 1U },
    {   6U, 20U, 0U,  1,  1, 0U },
    { 120U, 24U, 1U, -1,  1, 0U },
    {  38U, 40U, 2U,  1, -1, 0U },
    {  90U, 38U, 3U, -1, -1, 0U },
    {  56U, 12U, 1U,  1,  1, 0U },
    {  72U, 14U, 2U, -1,  1, 0U },
    {  28U, 28U, 0U,  1, -1, 0U },
    { 100U, 26U, 3U, -1, -1, 0U },
};

#define BOOT_SPARKLE_COUNT ((uint8_t)(sizeof(boot_sparkles) / sizeof(boot_sparkles[0])))

static void Boot_DrawSparkle(uint8_t x, uint8_t y, uint8_t big)
{
    if (x >= OLED_WIDTH || y >= OLED_HEIGHT)
    {
        return;
    }

    OLED_DrawPixel(x, y, 1U);

    if (x > 0U)
    {
        OLED_DrawPixel((uint8_t)(x - 1U), y, 1U);
    }

    if ((uint8_t)(x + 1U) < OLED_WIDTH)
    {
        OLED_DrawPixel((uint8_t)(x + 1U), y, 1U);
    }

    if (y > 0U)
    {
        OLED_DrawPixel(x, (uint8_t)(y - 1U), 1U);
    }

    if ((uint8_t)(y + 1U) < OLED_HEIGHT)
    {
        OLED_DrawPixel(x, (uint8_t)(y + 1U), 1U);
    }

    if (big == 0U)
    {
        return;
    }

    if (x > 0U && y > 0U)
    {
        OLED_DrawPixel((uint8_t)(x - 1U), (uint8_t)(y - 1U), 1U);
    }

    if ((uint8_t)(x + 1U) < OLED_WIDTH && y > 0U)
    {
        OLED_DrawPixel((uint8_t)(x + 1U), (uint8_t)(y - 1U), 1U);
    }

    if (x > 0U && (uint8_t)(y + 1U) < OLED_HEIGHT)
    {
        OLED_DrawPixel((uint8_t)(x - 1U), (uint8_t)(y + 1U), 1U);
    }

    if ((uint8_t)(x + 1U) < OLED_WIDTH && (uint8_t)(y + 1U) < OLED_HEIGHT)
    {
        OLED_DrawPixel((uint8_t)(x + 1U), (uint8_t)(y + 1U), 1U);
    }
}

static void Boot_DrawReadyFrame(void)
{
    Boot_DrawLogoFrame(7U, 0U, 0U, 100U);
    Display_SetCursor(40U, 57U);
    Display_WriteStringScaled("READY", 1U, 1U, 1U);
}

static void Boot_PlayCompletionSparkles(void)
{
    uint8_t frame;
    uint8_t i;
    int16_t x;
    int16_t y;

    for (frame = 0U; frame < 14U; frame++)
    {
        Boot_DrawReadyFrame();

        for (i = 0U; i < BOOT_SPARKLE_COUNT; i++)
        {
            if (((frame + boot_sparkles[i].phase) & 3U) == 1U)
            {
                continue;
            }

            x = (int16_t)boot_sparkles[i].x0 + (((int16_t)frame * boot_sparkles[i].dx) / 2);
            y = (int16_t)boot_sparkles[i].y0 + (((int16_t)frame * boot_sparkles[i].dy) / 2);

            if (x >= 0 && y >= 0 && x < OLED_WIDTH && y < OLED_HEIGHT)
            {
                Boot_DrawSparkle((uint8_t)x, (uint8_t)y, boot_sparkles[i].big);
            }
        }

        if (frame < 7U)
        {
            Boot_DrawSparkle((uint8_t)(52U + frame), (uint8_t)(10U + frame), 0U);
            Boot_DrawSparkle((uint8_t)(76U - frame), (uint8_t)(12U + frame), 0U);
            Boot_DrawSparkle((uint8_t)(64U + ((frame & 3U) - 1U)), (uint8_t)(8U + (frame * 2U)), 1U);
        }

        Display_Update();
        HAL_Delay(35);
    }
}

void Display_PlayBootAnimation(void)
{
    uint8_t i;
    uint8_t slide;
    uint8_t scan_y;
    uint8_t underline_w;
    uint8_t logo_w;
    uint8_t progress;

    logo_w = Boot_TextWidth("ANYLOAD", 7U, 2U, 2U);

    for (scan_y = 0U; scan_y < OLED_HEIGHT; scan_y = (uint8_t)(scan_y + 4U))
    {
        Display_Clear();
        OLED_FillRect(0U, scan_y, OLED_WIDTH, 3U, 1U);
        Display_Update();
        HAL_Delay(18);
    }

    HAL_Delay(60);

    for (i = 1U; i <= 7U; i++)
    {
        for (slide = 10U; slide > 0U; slide = (uint8_t)(slide - 2U))
        {
            Boot_DrawLogoFrame(i, slide, 0U, 0U);
            HAL_Delay(16);
        }

        Boot_DrawLogoFrame(i, 0U, 0U, 0U);
        HAL_Delay(28);
    }

    for (underline_w = 4U; underline_w <= logo_w; underline_w = (uint8_t)(underline_w + 4U))
    {
        Boot_DrawLogoFrame(7U, 0U, underline_w, 0U);
        HAL_Delay(22);
    }

    for (progress = 0U; progress <= 100U; progress = (uint8_t)(progress + 2U))
    {
        Boot_DrawLogoFrame(7U, 0U, logo_w, progress);
        HAL_Delay(24);
    }

    Boot_DrawReadyFrame();
    Boot_PlayCompletionSparkles();
}
