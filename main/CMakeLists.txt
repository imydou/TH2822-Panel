idf_component_register(SRCS "app_main.cpp" "board.c" "ui.cpp" "usb_transport.cpp"
    "../core/meter.cpp" "../core/i18n.cpp" "../core/i18n_generated.cpp"
    "../fonts/panel_cjk_14.c" "../fonts/panel_cjk_20.c" "../fonts/panel_cjk_24.c"
    INCLUDE_DIRS "." "../core" REQUIRES esp_lcd driver esp_timer usb nvs_flash)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)

target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)
