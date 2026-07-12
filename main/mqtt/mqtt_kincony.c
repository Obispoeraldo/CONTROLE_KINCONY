/*
 * mqtt_kincony.c
 *
 * MQTT do Controle Kincony.
 *
 * Funcao deste modulo:
 * - conectar ao broker configurado (qualquer broker, nao so o Mosquitto);
 * - receber comandos MQTT;
 * - encaminhar comandos para a maquina de estado em logica_controle.c;
 * - publicar monitoramento de entradas, saidas, estados e falhas.
 *
 * IMPORTANTE:
 * Este arquivo NAO aciona saidas diretamente.
 * Quem decide se pode ligar/desligar eh a logica_controle.
 */

#include "mqtt_kincony.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "esp_log.h"
#include "mqtt_client.h"

#include "entradas_kincony.h"
#include "saidas_digitais_kincony.h"
#include "logica_controle.h"
#include "wifi_kincony.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_crt_bundle.h"

#include "cJSON.h"
#include "esp_timer.h"

#include "esp_system.h"
#include "ota_github.h"

// Criado por Daniel Montanher - RTC DS1307 + 4 timers persistentes de aeradores. Integrado por
// Eraldo Bispo e Daniel Montanher a um servico de tempo unico (ver docs/RELATORIO_INTEGRACAO_ERALDO_DANIEL.md)
#include "rtc_ds1307.h"

extern uint8_t versao_firmware_atual;

static void tratar_comando_cmd_json(const char *payload);

#define TAG "MQTT_KINCONY"

#define MQTT_QOS_COMANDO        1
#define MQTT_QOS_MONITORAMENTO  1
#define MQTT_RETAIN_STATUS      1
#define MQTT_RETAIN_MONITOR     0

static esp_mqtt_client_handle_t mqtt_client = NULL;
static bool mqtt_conectado = false;
static TickType_t mqtt_tick_ultima_publicacao = 0;
// Editado por Eraldo Bispo - 11/07/2026 - intervalo de publicacao automatica configuravel
// pelo painel web em tempo real (sem reinicializar). Valor inicial: 5000 ms.
static volatile uint32_t mqtt_intervalo_ms = 5000;
// Editado por Eraldo Bispo — guarda a URI do broker configurada (pode ser qualquer broker, nao
// so o Mosquitto), para o log de "MQTT_EVENT_CONNECTED" mostrar o broker real.
static char mqtt_broker_uri[128] = {0};
// Criado por Eraldo Bispo - 23/06/2026 - precisa ser estatico (nao variavel local) porque
// esp_mqtt_client_config_t.session.last_will.msg guarda so o ponteiro, que tem que continuar
// valido durante toda a vida do client MQTT (ate a proxima reconexao, quando reconstruimos).
static char mqtt_lwt_payload[96] = {0};

static bool topico_igual(const esp_mqtt_event_handle_t event, const char *topico)
{
    size_t tam_topico = strlen(topico);

    if ((size_t)event->topic_len != tam_topico)
    {
        return false;
    }

    return (strncmp(event->topic, topico, event->topic_len) == 0);
}

static void copiar_evento_para_string(char *destino, size_t tamanho_destino, const char *origem, int tamanho_origem)
{
    if (destino == NULL || tamanho_destino == 0)
    {
        return;
    }

    size_t copiar = (tamanho_origem < (int)(tamanho_destino - 1)) ? (size_t)tamanho_origem : (tamanho_destino - 1);
    memcpy(destino, origem, copiar);
    destino[copiar] = '\0';
}

static void publicar_ack_comando(
    uint8_t grupo,
    const char *acao,
    esp_err_t ret
)
{
    char payload[256];

    snprintf(
        payload,
        sizeof(payload),
        "{"
            "\"type\":\"command_ack\","
            "\"group\":%u,"
            "\"action\":\"%s\","
            "\"result\":\"%s\","
            "\"remote\":%s"
        "}",
        grupo,
        acao,
        (ret == ESP_OK) ? "ok" : "blocked",
        Logica_Controle_IsRemoto() ? "true" : "false"
    );

    Mqtt_Kincony_Publicar(
        MQTT_TOPIC_STATE,
        payload
    );
}


// Editado por Eraldo Bispo - 23/06/2026 - acrescenta o IP atual da interface STA no payload de
// status, para localizar o ESP32 na rede apos reboot/DHCP renovar o IP (Wifi_Kincony_GetIpAtual
// nunca devolve "0.0.0.0": fica string vazia se a STA nao estiver conectada).
static void montar_status_payload(char *buffer, size_t tamanho, const char *status)
{
    char ip_atual[16];
    Wifi_Kincony_GetIpAtual(ip_atual, sizeof(ip_atual));

    snprintf(buffer, tamanho,
             "{\"status\":\"%s\",\"fw\":\"%s\",\"IP\":\"%s\"}",
             status,
             FIRMWARE_VERSION,
             ip_atual);
}

// Criado por Eraldo Bispo - 23/06/2026 - callback unico registrado por outro modulo (ex
// modbus_rs485_mqtt) para republicar seus proprios topicos retidos a cada reconexao MQTT.
static mqtt_kincony_callback_conectado_t callback_conectado = NULL;

void Mqtt_Kincony_RegistrarCallbackConectado(mqtt_kincony_callback_conectado_t callback)
{
    callback_conectado = callback;
}

static void Mqtt_Kincony_EventHandler(
    void *handler_args,
    esp_event_base_t base,
    int32_t event_id,
    void *event_data
)
{
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id)
    {
        case MQTT_EVENT_CONNECTED:
        {
            mqtt_conectado = true;
            ESP_LOGI(TAG, "MQTT conectado ao broker: %s", mqtt_broker_uri);

            char status_payload[96];
            montar_status_payload(status_payload, sizeof(status_payload), "online");

            // Editado por Eraldo Bispo - 11/07/2026 - publicacao sem retain: clientes que
            // assinarem depois nao devem receber estado antigo como se fosse atual.
            esp_mqtt_client_publish(
                mqtt_client,
                MQTT_TOPIC_STATUS,
                status_payload,
                0,
                MQTT_QOS_MONITORAMENTO,
                MQTT_RETAIN_MONITOR
            );

            esp_mqtt_client_subscribe(mqtt_client, MQTT_TOPIC_CMD, MQTT_QOS_COMANDO);
            ESP_LOGI(TAG, "Inscrito: %s", MQTT_TOPIC_CMD);

            // Editado por Eraldo Bispo e Daniel Montanher - integracao - a sincronizacao de hora
            // (NTP + RTC DS1307) NAO depende mais da conexao MQTT: ela roda desde o boot, assim
            // que o WiFi conecta (ver Ntp_Kincony_Iniciar() em main.c). Chamar
            // RTC_DS1307_SolicitarSincronizacaoInternet() aqui foi removido porque essa funcao
            // abria um segundo cliente SNTP concorrente com o de ntp_kincony.c - ver
            // docs/RELATORIO_INTEGRACAO_ERALDO_DANIEL.md, secao "Servico de tempo".

            Mqtt_Kincony_PublicarMonitoramento();

            if (callback_conectado != NULL)
            {
                callback_conectado();
            }

            break;
        }

        case MQTT_EVENT_DISCONNECTED:
        {
            mqtt_conectado = false;
            ESP_LOGW(TAG, "MQTT desconectado");
            break;
        }

case MQTT_EVENT_DATA:
{
    char topico[96];
    char payload[256];

    copiar_evento_para_string(
        topico,
        sizeof(topico),
        event->topic,
        event->topic_len
    );

    copiar_evento_para_string(
        payload,
        sizeof(payload),
        event->data,
        event->data_len
    );

    ESP_LOGI(TAG, "Topico: %s", topico);
    ESP_LOGI(TAG, "Payload: %s", payload);

    if (topico_igual(event, MQTT_TOPIC_CMD))
    {
        tratar_comando_cmd_json(payload);
    }
    else
    {
        ESP_LOGW(TAG, "Topico de comando nao tratado: %s", topico);
    }

    Mqtt_Kincony_PublicarMonitoramento();
    break;
}

        case MQTT_EVENT_ERROR:
        {
            mqtt_conectado = false;
            ESP_LOGE(TAG, "Erro MQTT");
            break;
        }

        default:
            break;
    }
}

esp_err_t Mqtt_Kincony_Init(const char *broker_uri, const char *usuario, const char *senha)
{
    if (broker_uri == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    strncpy(mqtt_broker_uri, broker_uri, sizeof(mqtt_broker_uri) - 1);
    mqtt_broker_uri[sizeof(mqtt_broker_uri) - 1] = '\0';

    // Editado por Eraldo Bispo - 23/06/2026 - LWT agora inclui o IP atual (montado com a mesma
    // funcao usada no status online), em vez de string fixa sem IP. Limitacao conhecida: se o
    // IP mudar enquanto o MQTT permanece conectado, o LWT so reflete o IP novo na proxima vez
    // que Mqtt_Kincony_Init() rodar (reboot ou reconexao manual) - nao da pra atualizar o LWT
    // de um client ja conectado no esp-mqtt sem recriar o client.
    montar_status_payload(mqtt_lwt_payload, sizeof(mqtt_lwt_payload), "offline");

esp_mqtt_client_config_t mqtt_config = {
    .broker.address.uri = broker_uri,
    .broker.verification.crt_bundle_attach = esp_crt_bundle_attach,

    .session.last_will.topic = MQTT_TOPIC_STATUS,
    .session.last_will.msg = mqtt_lwt_payload,
    .session.last_will.msg_len = strlen(mqtt_lwt_payload),
    .session.last_will.qos = MQTT_QOS_MONITORAMENTO,
    .session.last_will.retain = true,
};

    // Editado por Eraldo Bispo — credenciais do broker agora vem do painel web (NVS), nao mais
    // fixas no codigo (ver docs/RELATORIO_INTEGRACAO_ERALDO_DANIEL.md, credencial removida do
    // historico "administrador"/"Administrador2026" - recomendado rotacionar no broker). So
    // define se nao vier vazio (broker sem autenticacao fica sem credenciais).
    if (usuario != NULL && strlen(usuario) > 0)
    {
        mqtt_config.credentials.username = usuario;
    }

    if (senha != NULL && strlen(senha) > 0)
    {
        mqtt_config.credentials.authentication.password = senha;
    }

    mqtt_client = esp_mqtt_client_init(&mqtt_config);

    if (mqtt_client == NULL)
    {
        ESP_LOGE(TAG, "Erro ao criar cliente MQTT");
        return ESP_FAIL;
    }

    esp_err_t ret = esp_mqtt_client_register_event(
        mqtt_client,
        ESP_EVENT_ANY_ID,
        Mqtt_Kincony_EventHandler,
        NULL
    );

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Erro ao registrar evento MQTT");
        return ret;
    }

    ret = esp_mqtt_client_start(mqtt_client);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Erro ao iniciar MQTT");
        return ret;
    }

    ESP_LOGI(TAG, "MQTT iniciado no broker: %s", broker_uri);

    return ESP_OK;
}

void Mqtt_Kincony_Processar(void)
{
    if (!mqtt_conectado)
    {
        return;
    }

    TickType_t agora = xTaskGetTickCount();

    if ((agora - mqtt_tick_ultima_publicacao) >= pdMS_TO_TICKS(mqtt_intervalo_ms))
    {
        mqtt_tick_ultima_publicacao = agora;
        Mqtt_Kincony_PublicarMonitoramento();
    }
}

esp_err_t Mqtt_Kincony_Publicar(const char *topico, const char *mensagem)
{
    if (mqtt_client == NULL || topico == NULL || mensagem == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (!mqtt_conectado)
    {
        return ESP_FAIL;
    }

    int msg_id = esp_mqtt_client_publish(
        mqtt_client,
        topico,
        mensagem,
        0,
        MQTT_QOS_MONITORAMENTO,
        MQTT_RETAIN_MONITOR
    );

    if (msg_id < 0)
    {
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t Mqtt_Kincony_PublicarRetido(const char *topico, const char *mensagem)
{
    if (mqtt_client == NULL || topico == NULL || mensagem == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (!mqtt_conectado)
    {
        return ESP_FAIL;
    }

    int msg_id = esp_mqtt_client_publish(
        mqtt_client,
        topico,
        mensagem,
        0,
        MQTT_QOS_MONITORAMENTO,
        MQTT_RETAIN_STATUS
    );

    if (msg_id < 0)
    {
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t Mqtt_Kincony_PublicarStatus(const char *status)
{
    char payload[96];

    montar_status_payload(payload, sizeof(payload), status);

    return Mqtt_Kincony_Publicar(MQTT_TOPIC_STATUS, payload);
}

esp_err_t Mqtt_Kincony_PublicarMonitoramento(void)
{
    if (!mqtt_conectado)
    {
        return ESP_FAIL;
    }

    char payload[768];
    int pos = 0;

    int64_t ts = esp_timer_get_time() / 1000000;

    pos += snprintf(payload + pos, sizeof(payload) - pos,
        "{"
        "\"id\":\"aerador-01\","
        "\"ts\":%lld,"
        "\"mode\":\"%s\","
        "\"online\":true,"
        "\"alarm\":%s,"
        "\"oxygen\":0,"
        "\"groups\":[",
        ts,
        Logica_Controle_IsRemoto() ? "remoto" : "local",
        Logica_Controle_IsFalhaGeral() ? "true" : "false"
    );

    for (uint8_t i = 0; i < LOGICA_CONTROLE_NUM_GRUPOS; i++)
    {
        bool out = Logica_Controle_GetComandoGrupo((logica_grupo_t)i);
        bool fb = Logica_Controle_GetFeedbackGrupo((logica_grupo_t)i);
        bool fault = Logica_Controle_GetFalhaGrupo((logica_grupo_t)i) != LOGICA_FALHA_NENHUMA;

        pos += snprintf(payload + pos, sizeof(payload) - pos,
            "%s{\"g\":%u,\"out\":%s,\"fb\":%s,\"fault\":%s,\"src\":\"manual\"}",
            (i == 0) ? "" : ",",
            i + 1,
            out ? "true" : "false",
            fb ? "true" : "false",
            fault ? "true" : "false"
        );
    }

    snprintf(payload + pos, sizeof(payload) - pos, "]}");

    return Mqtt_Kincony_Publicar(MQTT_TOPIC_STATE, payload);
}

bool Mqtt_Kincony_IsConectado(void)
{
    return mqtt_conectado;
}

// Editado por Eraldo Bispo - 11/07/2026 - intervalo aplicado em tempo real, sem reiniciar.
void Mqtt_Kincony_SetIntervaloMs(uint32_t ms)
{
    if (ms < 1000)   ms = 1000;
    if (ms > 180000) ms = 180000;
    mqtt_intervalo_ms = ms;
}

uint32_t Mqtt_Kincony_GetIntervaloMs(void)
{
    return mqtt_intervalo_ms;
}

static void tratar_comando_cmd_json(const char *payload)
{
    cJSON *root = cJSON_Parse(payload);

    if (root == NULL)
    {
        ESP_LOGW(TAG, "JSON invalido: %s", payload);
        return;
    }

    cJSON *group = cJSON_GetObjectItem(root, "group");
    cJSON *action = cJSON_GetObjectItem(root, "action");
    cJSON *src = cJSON_GetObjectItem(root, "src");

    if (!cJSON_IsString(action))
    {
        ESP_LOGW(TAG, "Payload CMD sem action valida");
        cJSON_Delete(root);
        return;
    }

    if (src != NULL && cJSON_IsString(src))
    {
        ESP_LOGI(TAG, "Comando origem: %s", src->valuestring);

        
    }

/*
 * Primeiro oferece o JSON ao modulo RTC/timers.
 *
 * Se o comando for timer_set, timer_get, rtc_sync etc.,
 * o proprio rtc_ds1307 trata e retorna uma resposta JSON.
 *
 * Se nao for um comando de RTC/timer, retorna
 * ESP_ERR_NOT_SUPPORTED e o MQTT continua tratando
 * reset, OTA e comandos manuais.
 */
char resposta_rtc[768] = {0};

esp_err_t ret_rtc = RTC_DS1307_ProcessarComandoMQTT(
    payload,
    resposta_rtc,
    sizeof(resposta_rtc)
);

if (ret_rtc != ESP_ERR_NOT_SUPPORTED)
{
    ESP_LOGI(
        TAG,
        "Comando RTC processado: ret=%s resposta=%s",
        esp_err_to_name(ret_rtc),
        resposta_rtc
    );

    if (resposta_rtc[0] != '\0')
    {
        Mqtt_Kincony_Publicar(
            MQTT_TOPIC_STATUS_RTC,
            resposta_rtc
        );
    }

    cJSON_Delete(root);
    return;
}

    if (strcmp(action->valuestring, "reset_esp") == 0)
    {
        Mqtt_Kincony_PublicarStatus("resetting");
        vTaskDelay(pdMS_TO_TICKS(500));

        cJSON_Delete(root);
                esp_restart();
        return;
    }

    if (strcmp(action->valuestring, "ota_enable") == 0)
    {
        versao_firmware_atual = 1;

        Mqtt_Kincony_Publicar(
            MQTT_TOPIC_STATE,
            "{\"ota_enable\":true,\"versao_firmware_atual\":1}"
        );

        cJSON_Delete(root);
        return;
    }
        if (strcmp(action->valuestring, "reset_faults") == 0)
{
    Logica_Controle_ResetarFalhas();

    Mqtt_Kincony_Publicar(
        MQTT_TOPIC_STATE,
        "{\"reset_faults\":true}"
    );

    cJSON_Delete(root);
    return;
}


    if (!cJSON_IsNumber(group))
    {
        ESP_LOGW(TAG, "Comando de grupo sem campo group");
        cJSON_Delete(root);
        return;
    }

    int grupo = group->valueint;
    bool ligar;

    if (strcmp(action->valuestring, "on") == 0)
    {
        ligar = true;
    }
    else if (strcmp(action->valuestring, "off") == 0)
    {
        ligar = false;
    }
    else
    {
        ESP_LOGW(TAG, "Action invalida: %s", action->valuestring);
        cJSON_Delete(root);
        return;
    }


if (grupo == 0)
{
    /*
     * Grupo zero representa todos os grupos.
     * O override manual so eh registrado quando
     * a logica realmente aceita o comando.
     */
    for (uint8_t i = 0; i < LOGICA_CONTROLE_NUM_GRUPOS; i++)
    {
        esp_err_t ret = Logica_Controle_SetComandoGrupo(
            (logica_grupo_t)i,
            ligar
        );

        if (ret == ESP_OK)
        {
            RTC_DS1307_RegistrarComandoManual(i + 1);
        }

        publicar_ack_comando(
            i + 1,
            ligar ? "ON" : "OFF",
            ret
        );
    }
}
else if (grupo >= 1 && grupo <= LOGICA_CONTROLE_NUM_GRUPOS)
{
    esp_err_t ret = Logica_Controle_SetComandoGrupo(
        (logica_grupo_t)(grupo - 1),
        ligar
    );

    if (ret == ESP_OK)
    {
        RTC_DS1307_RegistrarComandoManual((uint8_t)grupo);
    }

    publicar_ack_comando(
        (uint8_t)grupo,
        ligar ? "ON" : "OFF",
        ret
    );
}
else
{
    ESP_LOGW(TAG, "Grupo invalido: %d", grupo);

Mqtt_Kincony_Publicar(
    MQTT_TOPIC_STATE,
    "{"
        "\"type\":\"command_ack\","
        "\"result\":\"invalid_group\""
    "}"
);
}

    cJSON_Delete(root);
}