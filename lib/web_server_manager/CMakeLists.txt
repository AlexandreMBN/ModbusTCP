idf_component_register(
    SRCS 
        "src/web_server_manager.c"
        "src/wsm_auth.c"
        "src/wsm_handlers.c"
        "src/wsm_middleware.c"
        "src/wsm_static_files.c"
        "src/wsm_templates.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server esp_wifi
)