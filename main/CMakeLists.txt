idf_component_register(SRCS "ethernet_example_main.c" "weight.c"
                       PRIV_REQUIRES esp_netif esp_eth ethernet_init ads1235 console esp_timer
                       INCLUDE_DIRS ".")
