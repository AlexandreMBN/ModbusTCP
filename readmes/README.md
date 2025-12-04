# Webserver ESP32 Modularizado

Este projeto implementa um gerenciador de WiFi com interface web para ESP32, totalmente modularizado para facilitar manutenção e expansão.

## Estrutura dos Módulos

- **src/main.c**: Responsável apenas pela inicialização do sistema e chamada das funções principais dos módulos.
- **src/webserver.c / webserver.h**: Toda a lógica do servidor HTTP, handlers das rotas e inicialização do webserver.
- **src/wifi_manager.c / wifi_manager.h**: Funções para scan de redes, configuração e inicialização do WiFi em modo AP.
- **src/storage.c / storage.h**: Funções para salvar e carregar dados na NVS (por exemplo, estado de login).
- **src/spiffs_manager.c / spiffs_manager.h**: Inicialização e manipulação do SPIFFS.
- **src/config.c / config.h**: Definições de variáveis globais e configurações compartilhadas entre módulos.

## Fluxo de Inicialização
O arquivo `main.c` apenas chama as funções de inicialização dos módulos:
```c
void app_main(void) {
    nvs_flash_init();
    logged_in = load_login_state();
    init_spiffs();
    start_wifi_ap();
}
```

## Compilação e Upload
O projeto está pronto para ser compilado com PlatformIO:

1. Instale o [PlatformIO](https://platformio.org/)
2. Conecte seu ESP32 ao computador
3. Compile e faça upload com:
   ```
   pio run --target upload
   ```

## Observações
- Todos os arquivos `.c` e `.h` dos módulos estão na pasta `src/`.
- Para adicionar novas funcionalidades, crie novos módulos ou expanda os existentes.

---

Dúvidas ou sugestões? Abra uma issue ou entre em contato! 