#ifndef MQTT_KINCONY_H
#define MQTT_KINCONY_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"


//#define MQTT_KINCONY_BROKER_URI "mqtt://test.mosquitto.org:1883"
#define MQTT_KINCONY_BROKER_URI \
"mqtts://453e5f32c60a48529669859610ab6a88.s1.eu.hivemq.cloud:8883"

/* Topicos de monitoramento */
#define MQTT_BASE_TOPIC                 "aquapulse/fazenda01/acude01"

#define MQTT_TOPIC_STATE                MQTT_BASE_TOPIC "/state"
#define MQTT_TOPIC_CMD                  MQTT_BASE_TOPIC "/cmd"
#define MQTT_TOPIC_STATUS               MQTT_BASE_TOPIC "/status"

#define MQTT_PUBLICACAO_MONITORAMENTO_MS 1000

/* Mantido por compatibilidade com seu codigo antigo */
#define MQTT_TOPIC_COMANDO_SAIDA         MQTT_TOPIC_COMANDO_GRUPO

/* Periodo padrao para publicar monitoramento */
#define MQTT_PUBLICACAO_MONITORAMENTO_MS 1000

esp_err_t Mqtt_Kincony_Init(const char *broker_uri);
void Mqtt_Kincony_Processar(void);

esp_err_t Mqtt_Kincony_Publicar(const char *topico, const char *mensagem);
esp_err_t Mqtt_Kincony_PublicarStatus(const char *mensagem);
esp_err_t Mqtt_Kincony_PublicarEntradas(uint8_t entradas);
esp_err_t Mqtt_Kincony_PublicarSaidas(uint8_t saidas);
esp_err_t Mqtt_Kincony_PublicarControle(void);
esp_err_t Mqtt_Kincony_PublicarGrupos(void);
esp_err_t Mqtt_Kincony_PublicarFalhas(void);
esp_err_t Mqtt_Kincony_PublicarMonitoramento(void);

bool Mqtt_Kincony_IsConectado(void);

#endif
