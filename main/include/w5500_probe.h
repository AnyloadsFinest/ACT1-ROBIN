#pragma once

#include "esp_err.h"
#include <stdint.h>

/*
 * SPI register map matches:
 *   tools/ioLibrary_Driver/Ethernet/W5500/w5500.h  (WIZnet getVERSIONR / PHYCFGR_LNK_ON)
 *   ESP-IDF esp_eth/src/spi/w5500/w5500.h            (VDM, command_bits=16, address_bits=8, mode 0)
 *
 * Not using ioLibrary socket API — runtime driver is esp_eth (L2 MAC), same register access.
 */
#define W5500_SPI_REG(offset, bsb)  (((uint32_t)(offset) << 16) | ((uint32_t)(bsb) << 3))
#define W5500_REG_PHYCFGR_SPI       W5500_SPI_REG(0x002EU, 0U)
#define W5500_VERSIONR_EXPECTED     0x04U

/**
 * W5500 /RST (active low). Board has no external pull-up — release line as input + MCU pull-up.
 * Call after esp_eth init/start if the driver reconfigured the reset GPIO.
 */
void W5500_BoardRstRelease(void);

/** Assert /RST low, then release with internal pull-up (before SPI probe). */
void W5500_BoardRstPulse(void);

/** Raw SPI read of W5500 VERSIONR (0x04 = OK). Safe before esp_eth driver install. */
esp_err_t W5500_ProbeVersion(uint8_t *version_out);
