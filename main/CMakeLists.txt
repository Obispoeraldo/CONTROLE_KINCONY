idf_component_register(SRCS
    "main.c"
    "entradas_digitais/entradas_kincony.c"
    "saidas_digitais/saidas_digitais_kincony.c"
    "modbus_rtu_slave/modbus_rtu_slave_esp.c"
    "wifi/wifi_kincony.c"
    "mqtt/mqtt_kincony.c"
    "ota/ota_github.c"
    "logica_controle/logica_controle.c"
    # Criado por Eraldo Bispo — painel web de configuracao de WiFi/broker (copiado do projeto do Eraldo)
    "config_server/config_server_kincony.c"

    INCLUDE_DIRS
    "."
    "entradas_digitais"
    "saidas_digitais"
    "modbus_rtu_slave"
    "wifi"
    "mqtt"
    "ota"
    "logica_controle"
    "config_server"
    REQUIRES
    freertos
    driver
    esp-modbus
    esp_wifi
    esp_event
    nvs_flash
    mqtt
    esp_http_client
    esp_https_ota
    esp-tls
    json
    app_update
    # Criado por Eraldo Bispo — necessarios pelo painel web (config_server_kincony.c)
    esp_http_server
    esp_hw_support

    # Criado por Eraldo Bispo — logo AquaPulse embutida no firmware, servida em /logo.jpg
    EMBED_FILES
    "config_server/logo_aquapulse.jpg"
)