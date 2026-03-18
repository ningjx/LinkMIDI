idf_component_register(
    SRCS "src/midi_error.c" "src/event_bus.c" "src/midi_converter.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_timer
)