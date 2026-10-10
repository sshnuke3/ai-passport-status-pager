idf_component_register(
    SRCS "status_main.c"
         "status_model.c"
         "status_store.c"
         "status_export.c"
         "status_ui.c"
    INCLUDE_DIRS "."
    REQUIRES bsp console esp_timer nvs_flash
)

# 中文字体(由 assets/fonts/gen_fonts.sh 生成,勿手工编辑)
target_sources(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/app_font_32.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/app_font_16.c"
)