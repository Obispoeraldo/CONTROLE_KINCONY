#ifndef MQTT_KINCONY_H
#define MQTT_KINCONY_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#define MQTT_KINCONY_BROKER_URI      "mqtt://192.168.0.100:1883"
//#define MQTT_KINCONY_BROKER_URI "mqtt://192.168.0.100:1883"

#define MQTT_TOPIC_STATUS            "kincony/status"
#define MQTT_TOPIC_ENTRADAS          "kincony/entradas"
#define MQTT_TOPIC_SAIDAS            "kincony/saidas"
#define MQTT_TOPIC_COMANDO_SAIDA     "kincony/comando/saida"

esp_err_t Mqtt_Kincony_Init(const char *broker_uri);

esp_err_t Mqtt_Kincony_Publicar(const char *topico, const char *mensagem);
esp_err_t Mqtt_Kincony_PublicarStatus(const char *mensagem);
esp_err_t Mqtt_Kincony_PublicarEntradas(uint8_t entradas);
esp_err_t Mqtt_Kincony_PublicarSaidas(uint8_t saidas);

bool Mqtt_Kincony_IsConectado(void);

#endif