#ifndef BOARD_H
#define BOARD_H

/*
 * Production PCB map for the SCT-2200-class transmitter.
 * OLED and the five keys are already assigned. Everything else stays
 * unassigned until the schematic is locked — DigIo / Rs485 / Can / Eth
 * skip a pin when it is BOARD_GPIO_NONE.
 */

#define BOARD_GPIO_NONE         (-1)

#define BOARD_OUT_COUNT         3
#define BOARD_IN_COUNT          2

#define BOARD_OLED_SCL_GPIO     16  /* IO16, WROOM-1 module pad 9 */
#define BOARD_OLED_SDA_GPIO     17  /* IO17, WROOM-1 module pad 10 */

/*
 * Front-panel keys: schematic "IOxx" = ESP-IDF GPIO_NUM_xx (NOT module pad index).
 *
 *  Function | IO / GPIO | WROOM-1 pad
 *  UP       | IO14      | 22
 *  DOWN     | IO21      | 23
 *  CONFIRM  | IO47      | 24
 *  LEFT     | IO48      | 25
 *  RIGHT    | IO45      | 26
 */
#define BOARD_BTN_UP_GPIO       14
#define BOARD_BTN_DOWN_GPIO     21
#define BOARD_BTN_ENTER_GPIO    47
#define BOARD_BTN_LEFT_GPIO     48
#define BOARD_BTN_RIGHT_GPIO    45

/*
 * 1 = switch shorts GPIO to GND when pressed (internal pull-up, default).
 * 0 = pressed reads high (use external pull-down or invert in Button_IsPressed).
 */
#ifndef BOARD_BTN_ACTIVE_LOW
#define BOARD_BTN_ACTIVE_LOW    1
#endif

/*
 * DOUT1–3 → AQY282SX PhotoMOS (LED side on schematic DOUTx). GPIO high = LED on = output ON.
 * Series resistor on PCB sets LED current; CONTACT NC in F5.x inverts logical drive in software.
 */
#define BOARD_OUT1_GPIO         38
#define BOARD_OUT2_GPIO         39
#define BOARD_OUT3_GPIO         40
#ifndef BOARD_OUT_ACTIVE_HIGH
#define BOARD_OUT_ACTIVE_HIGH   1
#endif
/* 1 = IO38/39/40 toggle for bench (disable for production). */
#ifndef BOARD_DOUT_BENCH_TOGGLE
#define BOARD_DOUT_BENCH_TOGGLE 0
#endif
#ifndef BOARD_DOUT_BENCH_TOGGLE_MS
#define BOARD_DOUT_BENCH_TOGGLE_MS  10000U
#endif
/*
 * DI1–2 ← ISO1212DBQR OUT1/OUT2 (side1 / MCU). Field-active → OUT high (IEC 24 V sink typical).
 * EN tied high on PCB. 1 = contact closed / active in DigIo_GetIn().
 */
#define BOARD_IN1_GPIO          19
#define BOARD_IN2_GPIO          20
#ifndef BOARD_IN_ACTIVE_HIGH
#define BOARD_IN_ACTIVE_HIGH    1
#endif

#define BOARD_ADC_SPI_MOSI      11
#define BOARD_ADC_SPI_MISO      13
#define BOARD_ADC_SPI_SCLK      12
#define BOARD_ADC_SPI_CS        3
#define BOARD_ADC_DRDY_GPIO     8
#define BOARD_ADC_PWDN_GPIO     BOARD_GPIO_NONE

#define BOARD_ANA_SPI_MOSI      BOARD_GPIO_NONE
#define BOARD_ANA_SPI_SCLK      BOARD_GPIO_NONE
#define BOARD_ANA_SPI_CS        BOARD_GPIO_NONE

/*
 * CAN via CA-IS2062A (schematic IO = ESP GPIO):
 *   IO4 (CANTXD) -> TXD    IO5 (CANRXD) <- RXD
 */
#define BOARD_CAN_TX_GPIO       4
#define BOARD_CAN_RX_GPIO       5

/*
 * RS-485 via CA-IS2092A:
 *   IO6 -> DI (RS485_TXD)   IO7 <- RO (RS485_RXD)   IO15 -> DE/RE (RS485_EN)
 */
#define BOARD_RS485_TX_GPIO     6
#define BOARD_RS485_RX_GPIO     7
#define BOARD_RS485_DE_GPIO     15
#define BOARD_RS485_UART_NUM    1

/*
 * W5500 on SPI2 (shared with ADS1235). Pin numbers = ESP-IDF gpio_num / schematic IOxx
 * on ESP32-S3-WROOM-1 — NOT the module package pad index (e.g. pad 10 = IO17, not IO10).
 *
 *  Function   | IO / GPIO | WROOM-1 pad
 *  MOSI       | IO11      | 19
 *  MISO       | IO13      | 21
 *  SCLK       | IO12      | 20
 *  W5500 CS   | IO10      | 18
 *  W5500 INT  | IO9       | 17
 *  W5500 RST  | IO46      | 16
 *  ADS1235 CS | IO3       | 15
 *  ADS DRDY   | IO8       | 12
 *
 * Kconfig: CONFIG_EXAMPLE_ETH_SPI_*_GPIO in config/act1-robin.defaults must match IO above.
 */
#define BOARD_ETH_SPI_MOSI      BOARD_ADC_SPI_MOSI
#define BOARD_ETH_SPI_MISO      BOARD_ADC_SPI_MISO
#define BOARD_ETH_SPI_SCLK      BOARD_ADC_SPI_SCLK
#define BOARD_ETH_SPI_CS        10
#define BOARD_ETH_INT_GPIO      9
#define BOARD_ETH_RST_GPIO      46   /* /RST active-low; no ext pull-up — use W5500_BoardRstRelease() */

/*
 * 1 = force off ETH PHY 1s log, CAN/RS485/DOUT bench traffic (ignores stale sdkconfig).
 * DI bench log (CONFIG_ACT1_DI_BENCH_LOG) is separate.
 */
#ifndef ACT1_SUPPRESS_BENCH_IO
#define ACT1_SUPPRESS_BENCH_IO  1
#endif

#define FIRMWARE_VERSION_STR    "1.0.2"

#endif
