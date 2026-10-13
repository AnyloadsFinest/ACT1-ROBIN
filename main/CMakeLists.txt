# ACT1 product firmware (OLED + menu + buttons)
idf_component_register(
    SRCS "act1_main.c"
         "src/display.c"
         "src/buttons.c"
         "src/menu.c"
         "src/menu_cal.c"
         "src/menu_wiz.c"
         "src/menu_digit.c"
         "src/params.c"
         "src/param_storage.c"
         "src/scale.c"
         "src/analog_out.c"
         "src/adc_front.c"
         "src/analog_dac.c"
         "src/dig_io.c"
         "src/outputs.c"
         "src/inputs.c"
         "src/remote_cmd.c"
         "src/comm.c"
         "src/rs485_uart.c"
         "src/can_bus.c"
         "src/robin_eth.c"
         "src/w5500_probe.c"
         "src/robin_net.c"
         "src/robin_wifi.c"
         "src/settings_core.c"
    PRIV_REQUIRES esp_driver_i2c esp_driver_gpio esp_driver_spi esp_driver_uart driver esp_timer nvs_flash esp_event
                  esp_netif esp_eth esp_wifi ethernet_init ads1235 log console json
    INCLUDE_DIRS "include"
)

# UART / monitor ADC test — swap with block above to use ethernet_example_main.c + weight.c
