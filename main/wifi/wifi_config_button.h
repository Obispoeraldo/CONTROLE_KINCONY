#ifndef WIFI_CONFIG_BUTTON_H
#define WIFI_CONFIG_BUTTON_H

#include "esp_err.h"

/*
 * wifi_config_button.h
 *
 * Botao fisico de configuracao de rede - usa o GPIO0 (botao "BOOT" nativo do ESP32),
 * livre neste projeto: o barramento I2C dos modulos de entrada/saida/RTC usa GPIO4 (SDA)
 * e GPIO15 (SCL), ver entradas_kincony.c. Nao usamos o botao RESET/EN (esse so reinicia o
 * ESP fisicamente, nao e lido por software).
 *
 * Leitura por polling (chamado a cada volta do loop principal, ~200ms), sem interrupcao e
 * sem bloquear: cada nivel de pressao dispara sua acao uma unica vez por pressao continua
 * (nao espera o botao ser solto - da feedback imediato ao segurar).
 *
 * Niveis (mantendo o botao pressionado continuamente):
 *   >= 5s  -> abre o portal de configuracao (WIFI_MODE_APSTA), SEM apagar a rede atual -
 *             o STA continua tentando reconectar em paralelo (ver wifi_kincony.c).
 *   >= 10s -> apaga so as credenciais WiFi salvas em NVS e abre o portal (util quando o
 *             SSID/senha antigos nao existem mais e nao vale a pena continuar tentando).
 *   >= 30s -> restaura a configuracao de fabrica completa (WiFi, broker MQTT, NTP) e
 *             reinicia o ESP.
 */

esp_err_t Wifi_Config_Button_Iniciar(void);

// Chamar a cada volta do loop principal (main.c). Nao bloqueia.
void Wifi_Config_Button_Processar(void);

#endif
