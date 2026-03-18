idf_component_register(
    SRCS
        "midi2net.c"
        "midi2net_mdns.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        lwip
        esp_netif
        esp_event
        esp_system
        freertos
)

