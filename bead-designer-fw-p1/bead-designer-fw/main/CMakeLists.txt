idf_component_register(
    SRCS
        "app_main.c"
        "input.c"
        "ui.c"
        "render.c"
        "palettes.c"
        "store.c"
        "export.c"
        "text.c"
        "menu.c"
        "board_style.c"
        "font_chicago.c"
        "palette_artkal_s.c"
        "palette_miyuki_delica.c"
        "board/board_freenove.c"
        "board/board_ws241.c"
    INCLUDE_DIRS "." "board"
    PRIV_REQUIRES driver esp_driver_gpio esp_driver_uart esp_driver_spi esp_lcd esp_adc nvs_flash esp_wifi esp_http_server esp_timer
)
