/*
 * modbus_rs485_mqtt.c
 *
 * Criado por Eraldo Bispo - 23/06/2026
 * Ver contrato dos topicos em modbus_rs485_mqtt.h
 */

#include "modbus_rs485_mqtt.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "modbus_rs485_config.h"
#include "modbus_rs485_master.h"
#include "mqtt_kincony.h"

static const char *TAG = "MODBUS_RS485_MQTT";

// Editado por Eraldo Bispo - 11/07/2026 - intervalo agora vem de Mqtt_Kincony_GetIntervaloMs()
// para sincronizar com o intervalo configurado pelo painel web.
#define MODBUS_RS485_MQTT_TOPICO_CONFIG    MQTT_BASE_TOPIC "/config-rs485"

static TickType_t tick_ultima_publicacao = 0;

static void publicar_config_rs485(const modbus_rs485_configuracao_t *config)
{
    char paridade[4];
    Modbus_RS485_Config_FormatarParidade(config, paridade, sizeof(paridade));

    char payload[96];
    snprintf(payload, sizeof(payload),
             "{\"status\":%s,\"bd\":%" PRIu32 ",\"Par\":\"%s\"}",
             Modbus_RS485_EstaHabilitado() ? "true" : "false",
             config->baud_rate,
             paridade);

    Mqtt_Kincony_Publicar(MODBUS_RS485_MQTT_TOPICO_CONFIG, payload);
}

// Editado por Eraldo Bispo - 23/06/2026 - publica um campo numerico por canal habilitado (ate
// MODBUS_RS485_MAX_CANAIS_POR_DISPOSITIVO), usando o nome configurado em campo_mqtt. Quando o
// dispositivo esta offline (ou o canal nunca leu um valor valido), publica 0 - nunca string
// "false"/"0", sempre tipos JSON nativos (bool e numero).
static void publicar_dispositivo(const modbus_dispositivo_t *dispositivo)
{
    if (!dispositivo->habilitado || strlen(dispositivo->identificador_mqtt) == 0)
    {
        return;
    }

    char topico[96];
    snprintf(topico, sizeof(topico), "%s/%s", MQTT_BASE_TOPIC, dispositivo->identificador_mqtt);

    char payload[256];
    int posicao = 0;

    posicao += snprintf(payload + posicao, sizeof(payload) - posicao,
                         "{\"status\":%s,\"Slave-id\":%u",
                         dispositivo->online ? "true" : "false",
                         dispositivo->endereco_escravo);

    for (uint8_t i = 0; i < MODBUS_RS485_MAX_CANAIS_POR_DISPOSITIVO; i++)
    {
        const modbus_canal_t *canal = &dispositivo->canais[i];

        if (!canal->habilitado || strlen(canal->campo_mqtt) == 0)
        {
            continue;
        }

        float valor_publicado = (dispositivo->online && canal->ultimo_valor_valido) ? canal->ultimo_valor : 0.0f;

        posicao += snprintf(payload + posicao, sizeof(payload) - posicao,
                             ",\"%s\":%.*f",
                             canal->campo_mqtt, canal->casas_decimais, valor_publicado);
    }

    snprintf(payload + posicao, sizeof(payload) - posicao, "}");

    Mqtt_Kincony_Publicar(topico, payload);
}

static void publicar_tudo(void)
{
    modbus_rs485_configuracao_t instantaneo;

    if (Modbus_RS485_ObterSnapshot(&instantaneo) != ESP_OK)
    {
        ESP_LOGW(TAG, "Nao foi possivel obter o estado atual do Modbus RS485 para publicar no MQTT");
        return;
    }

    publicar_config_rs485(&instantaneo);

    for (uint8_t i = 0; i < MODBUS_RS485_MAX_DISPOSITIVOS; i++)
    {
        publicar_dispositivo(&instantaneo.dispositivos[i]);
    }
}

void Modbus_RS485_Mqtt_Iniciar(void)
{
    // Editado por Eraldo Bispo - 23/06/2026 - garante que config-rs485 e as sondas (ambos
    // publicados com retain=true via Mqtt_Kincony_PublicarRetido) sejam republicados a cada
    // reconexao MQTT, alem da publicacao periodica de Modbus_RS485_Mqtt_Processar().
    Mqtt_Kincony_RegistrarCallbackConectado(publicar_tudo);
}

void Modbus_RS485_Mqtt_Processar(void)
{
    if (!Mqtt_Kincony_IsConectado())
    {
        return;
    }

    TickType_t agora = xTaskGetTickCount();

    if ((agora - tick_ultima_publicacao) < pdMS_TO_TICKS(Mqtt_Kincony_GetIntervaloMs()))
    {
        return;
    }

    tick_ultima_publicacao = agora;

    publicar_tudo();
}
