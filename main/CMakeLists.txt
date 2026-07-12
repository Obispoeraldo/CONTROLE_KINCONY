idf_component_register(SRCS
    "main.c"
    "entradas_digitais/entradas_kincony.c"
    "saidas_digitais/saidas_digitais_kincony.c"
    "modbus_rtu_slave/modbus_rtu_slave_esp.c"
    # Editado por Eraldo Bispo - 23/06/2026 - Modbus RTU MASTER generico e configuravel pela
    # RS485 da KC868-A6 (N dispositivos x canais, substitui o teste fixo do Novus N1200).
    "modbus_master/modbus_rs485_config.c"
    "modbus_master/modbus_rs485_master.c"
    "modbus_master/modbus_rs485_mqtt.c"
    "modbus_master/modbus_rs485_web.c"
    "wifi/wifi_kincony.c"
    "mqtt/mqtt_kincony.c"
    "ota/ota_github.c"
    "logica_controle/logica_controle.c"
    # Criado por Daniel Montanher - RTC DS1307 com bateria + 4 timers persistentes de aeradores
    "rtc/rtc_ds1307.c"
    # Criado por Eraldo Bispo — painel web de configuracao de WiFi/broker (copiado do projeto do Eraldo)
    "config_server/config_server_kincony.c"
    # Criado por Eraldo Bispo - 24/06/2026 - sincronizacao de data/hora via NTP; integrado com o
    # RTC DS1307 de Daniel (RTC fornece a hora no boot/sem rede, NTP corrige o RTC quando ha
    # internet - ver docs/RELATORIO_INTEGRACAO_ERALDO_DANIEL.md, secao "Servico de tempo")
    "ntp/ntp_kincony.c"

    INCLUDE_DIRS
    "."
    "entradas_digitais"
    "saidas_digitais"
    "modbus_rtu_slave"
    "modbus_master"
    "wifi"
    "mqtt"
    "ota"
    "logica_controle"
    "rtc"
    "config_server"
    "ntp"
    # Editado por Eraldo Bispo — corrigidos nomes dos componentes gerenciados para IDF v6: esp-mqtt e cjson saíram do core
    REQUIRES
    freertos
    driver
    esp-modbus
    esp_wifi
    esp_event
    nvs_flash
    esp-mqtt
    esp_http_client
    esp_https_ota
    esp-tls
    cjson
    app_update
    # Criado por Eraldo Bispo — necessarios pelo painel web (config_server_kincony.c)
    esp_http_server
    esp_hw_support
    # Criado por Eraldo Bispo - 24/06/2026 - esp_netif_sntp_init/start (ntp_kincony.c)
    esp_netif

    # Criado por Eraldo Bispo — logo AquaPulse embutida no firmware, servida em /logo.jpg
    EMBED_FILES
    "config_server/logo_aquapulse.jpg"
)