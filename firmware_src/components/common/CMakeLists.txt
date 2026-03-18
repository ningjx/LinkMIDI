idf_component_register(
    SRCS "src/midi_error.c" "src/event_bus.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_timer
)