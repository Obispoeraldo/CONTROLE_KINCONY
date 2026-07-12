#ifndef MQTT_KINCONY_H
#define MQTT_KINCONY_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"


//#define MQTT_KINCONY_BROKER_URI "mqtt://test.mosquitto.org:1883"
#define MQTT_KINCONY_BROKER_URI \
"mqtts://453e5f32c60a48529669859610ab6a88.s1.eu.hivemq.cloud:8883"

/* Saida — publicacoes do controlador para o broker */
#define MQTT_BASE_TOPIC_OUT             "delinova/o/aquapulse/fazenda01/acude01"
/* Entrada — comandos enviados da aplicacao para o controlador */
#define MQTT_BASE_TOPIC_IN              "delinova/i/aquapulse/fazenda01/acude01"
/* Alias para modulos que publicam dados (ex: modbus_rs485_mqtt.c) */
#define MQTT_BASE_TOPIC                 MQTT_BASE_TOPIC_OUT

#define MQTT_TOPIC_STATE                MQTT_BASE_TOPIC_OUT "/state"
#define MQTT_TOPIC_STATUS               MQTT_BASE_TOPIC_OUT "/status"
// Criado por Daniel Montanher - resposta/estado dos comandos de RTC e timers de aeradores
#define MQTT_TOPIC_STATUS_RTC           MQTT_TOPIC_STATUS "/rtc"
#define MQTT_TOPIC_CMD                  MQTT_BASE_TOPIC_IN  "/cmd"

/* Periodo padrao para publicar monitoramento */
#define MQTT_PUBLICACAO_MONITORAMENTO_MS 1000

// Editado por Eraldo Bispo — usuario/senha agora configuraveis pelo painel web (antes fixos no
// codigo). Passe string vazia para nao enviar credenciais (broker sem autenticacao).
esp_err_t Mqtt_Kincony_Init(const char *broker_uri, const char *usuario, const char *senha);

// Editado por Eraldo Bispo - 11/07/2026 - intervalo de publicacao automatica configuravel
// pelo painel web sem reinicializar o controlador. Clampeado em [1000, 180000] ms.
// GetIntervaloMs() e usado por modulos externos (ex: modbus_rs485_mqtt.c) para sincronizar
// o proprio temporizador com o mesmo intervalo configurado.
void Mqtt_Kincony_SetIntervaloMs(uint32_t ms);
uint32_t Mqtt_Kincony_GetIntervaloMs(void);
void Mqtt_Kincony_Processar(void);

esp_err_t Mqtt_Kincony_Publicar(const char *topico, const char *mensagem);

// Criado por Eraldo Bispo - 23/06/2026 - igual a Mqtt_Kincony_Publicar(), mas com retain=true.
// Usado pelos topicos que devem "lembrar" o ultimo valor para quem assinar depois (ex:
// config-rs485 e sondas do motor Modbus RS485, ver modbus_rs485_mqtt.c). Mqtt_Kincony_Publicar()
// continua sem retain (MQTT_RETAIN_MONITOR=0), sem mudar nenhum comportamento existente.
esp_err_t Mqtt_Kincony_PublicarRetido(const char *topico, const char *mensagem);
esp_err_t Mqtt_Kincony_PublicarStatus(const char *mensagem);
esp_err_t Mqtt_Kincony_PublicarEntradas(uint8_t entradas);
esp_err_t Mqtt_Kincony_PublicarSaidas(uint8_t saidas);
esp_err_t Mqtt_Kincony_PublicarControle(void);
esp_err_t Mqtt_Kincony_PublicarGrupos(void);
esp_err_t Mqtt_Kincony_PublicarFalhas(void);
esp_err_t Mqtt_Kincony_PublicarMonitoramento(void);

bool Mqtt_Kincony_IsConectado(void);

// Criado por Eraldo Bispo - 23/06/2026 - permite outros modulos (ex: modbus_rs485_mqtt) se
// registrarem para publicar de novo seus proprios topicos retidos (config-rs485, sondas, etc)
// toda vez que o MQTT reconectar, sem o mqtt_kincony.c precisar conhecer esses modulos.
typedef void (*mqtt_kincony_callback_conectado_t)(void);
void Mqtt_Kincony_RegistrarCallbackConectado(mqtt_kincony_callback_conectado_t callback);

#endif
