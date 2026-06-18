#ifndef CONFIG_SERVER_KINCONY_H
#define CONFIG_SERVER_KINCONY_H

#include <stddef.h>
#include "esp_err.h"

/*
 * config_server_kincony.h
 *
 * Criado por Eraldo Bispo.
 * Guarda WiFi e broker MQTT em NVS (sobrevive a reinicializacoes), usando os
 * valores do menuconfig (Kconfig.projbuild) apenas como padrao de fabrica na
 * primeira vez que o ESP liga. Expoe um painel web (HTTP) para editar esses
 * valores remotamente, protegido por usuario/senha.
 *
 * Ordem de uso no main.c:
 *   1. Config_Server_Kincony_Iniciar()      -> inicializa NVS e carrega valores
 *   2. Wifi_Kincony_Init(ssid, senha)        -> usa os getters abaixo
 *   3. Config_Server_Kincony_IniciarHttp()   -> inicia o painel (depois do WiFi)
 *   4. Mqtt_Kincony_Init(broker, usuario, senha) -> usa os getters de broker/credenciais MQTT
 *
 * Acesso ao painel: http://<IP_DO_ESP>/  (IP aparece no log do monitor serial
 * apos a mensagem "WiFi conectado").
 * Login padrao: usuario "aquapulse", senha "aquapulse2026".
 */

esp_err_t Config_Server_Kincony_Iniciar(void);
esp_err_t Config_Server_Kincony_IniciarHttp(void);

void Config_Server_Kincony_GetWifiSsid(char *destino, size_t tamanho);
void Config_Server_Kincony_GetWifiSenha(char *destino, size_t tamanho);
void Config_Server_Kincony_GetBrokerUri(char *destino, size_t tamanho);

// Criado por Eraldo Bispo — credenciais opcionais do broker MQTT, editaveis pelo painel web
void Config_Server_Kincony_GetMqttUsuario(char *destino, size_t tamanho);
void Config_Server_Kincony_GetMqttSenha(char *destino, size_t tamanho);

// Criado por Eraldo Bispo — restaura o WiFi anterior (salvo automaticamente antes de qualquer troca pelo painel).
// Retorna ESP_OK se havia um backup e foi restaurado, ESP_FAIL se nao havia nada para restaurar.
esp_err_t Config_Server_Kincony_RestaurarBackupWifi(void);

#endif
