# Component makefile for AP Manager Library
# Para projetos que usam ESP-IDF make (versões antigas)

COMPONENT_SRCDIRS := src
COMPONENT_ADD_INCLUDEDIRS := include

# Dependências necessárias
COMPONENT_DEPENDS := esp_wifi esp_netif esp_event esp_system nvs_flash spiffs json

# Opcional: flags de compilação específicos
CFLAGS += -DAP_MANAGER_VERSION=\"1.0.0\"