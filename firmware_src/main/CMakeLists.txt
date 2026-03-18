idf_component_register(SRCS "main.c" "wifi_manager.c"
                       PRIV_REQUIRES spi_flash esp_wifi esp_netif esp_event nvs_flash 
                                     network_midi2 usb_midi_host
                       INCLUDE_DIRS "")
