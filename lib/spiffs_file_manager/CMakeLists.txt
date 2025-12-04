# SPIFFS File Manager Library
# Uma biblioteca completa para gerenciamento de arquivos SPIFFS e servidor web no ESP32

cmake_minimum_required(VERSION 3.16)

# Definir componente da biblioteca
idf_component_register(
    SRCS 
        "src/spiffs_file_manager.c"
    
    INCLUDE_DIRS 
        "include"
    
    REQUIRES 
        esp_http_server
        esp_spiffs
        nvs_flash
        json
        esp_netif
        esp_wifi
        
    PRIV_REQUIRES
        log
        esp_common
)

# Informações da biblioteca
set(COMPONENT_NAME "spiffs_file_manager")
set(COMPONENT_VERSION "1.0.0")

# Definir configurações opcionais de compilação
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    SPIFFS_FILE_MANAGER_VERSION="${COMPONENT_VERSION}"
)

# Configurações específicas para desenvolvimento
if(CONFIG_SPIFFS_FILE_MANAGER_DEBUG_LOGS)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        SPIFFS_FILE_MANAGER_DEBUG=1
    )
endif()

# Otimizações para tamanho quando em produção
if(CONFIG_SPIFFS_FILE_MANAGER_OPTIMIZE_SIZE)
    target_compile_options(${COMPONENT_LIB} PRIVATE
        -Os
        -ffunction-sections
        -fdata-sections
    )
endif()

# Configurações de cache
if(CONFIG_SPIFFS_FILE_MANAGER_CACHE_SIZE)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        SPIFFS_FILE_MANAGER_CACHE_SIZE=${CONFIG_SPIFFS_FILE_MANAGER_CACHE_SIZE}
    )
endif()

# Log da compilação
message(STATUS "Building SPIFFS File Manager Library v${COMPONENT_VERSION}")
message(STATUS "  - Include dirs: ${CMAKE_CURRENT_SOURCE_DIR}/include")
message(STATUS "  - Source files: src/spiffs_file_manager.c")
message(STATUS "  - Dependencies: esp_http_server, esp_spiffs, nvs_flash, json")