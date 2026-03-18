idf_component_register(SRCS "hello_world_main.c"
                       PRIV_REQUIRES spi_flash esp_wifi esp_netif esp_event network_midi2
                       INCLUDE_DIRS "")
