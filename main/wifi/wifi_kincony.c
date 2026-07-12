/*
 * wifi_kincony.c
 *
 * Driver para conexão Wi-Fi do firmware Kincony.
 * Inicializa NVS, rede Wi-Fi em modo STA e controla estado de conexão.
 *
 * Criado em: 2024-06-01
 * Autor: DANIEL
 * Versão: 1.0
 */
 
/*------------------------ EXEMPLO DE USO NO MAIN.C ------------------------
INCLUINDO O CABEÇALHO
#include "wifi_kincony.h"

void app_main(void)
{
-------------------------- INICIANDO O WIFI ---------------------------

    ESP_ERROR_CHECK(Wifi_Kincony_Init("NOME_DA_REDE", "SENHA_DA_REDE"));

    while (1)
    {
-------------------------- VERIFICANDO ESTADO DO WIFI ---------------------------

        if (Wifi_Kincony_IsConectado())
        {
            printf("WiFi conectado\n");
        }
        else if (Wifi_Kincony_IsFalha())
        {
            printf("Falha ao conectar WiFi\n");
        }
        else
        {
            printf("WiFi tentando conectar\n");
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}*/

#include "wifi_kincony.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define TAG "WIFI_KINCONY"

#define WIFI_BIT_CONECTADO     BIT0
#define WIFI_BIT_FALHA         BIT1

// Editado por Eraldo Bispo - 11/07/2026 - backoff exponencial para reconexao WiFi.
// Nunca desiste: apos queda de energia o roteador pode levar 30-120s pra voltar.
#define WIFI_BACKOFF_MIN_MS    1000
#define WIFI_BACKOFF_MAX_MS   30000

static EventGroupHandle_t wifi_event_group = NULL;

static bool wifi_conectado = false;
static bool wifi_falha = false;
static uint8_t wifi_tentativas = 0;
static uint32_t wifi_backoff_ms = WIFI_BACKOFF_MIN_MS;
static esp_timer_handle_t wifi_timer_reconexao = NULL;

static char wifi_ssid[32] = {0};
static char wifi_senha[64] = {0};

// Criado por Eraldo Bispo - 23/06/2026 - IP atual da interface STA, atualizado no evento
// IP_EVENT_STA_GOT_IP e limpo na desconexao, para Wifi_Kincony_GetIpAtual() expor sem precisar
// consultar a interface de rede diretamente (usado pelo status MQTT, ver mqtt_kincony.c).
static char wifi_ip_atual[16] = {0};

// Editado por Eraldo Bispo - 11/07/2026 - chamado pelo esp_timer apos o backoff:
// tenta reconectar sem bloquear nenhuma task.
static void wifi_reconectar_cb(void *arg)
{
    ESP_LOGW(TAG, "WiFi: tentando reconectar (proximo backoff: %lu s)...",
             (unsigned long)(wifi_backoff_ms / 1000));
    esp_wifi_connect();
}

static void Wifi_Kincony_EventHandler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data
)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }

    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        wifi_conectado = false;
        wifi_ip_atual[0] = '\0';

        if (wifi_tentativas < UINT8_MAX)
        {
            wifi_tentativas++;
        }

        // Backoff exponencial: usa o valor atual e dobra para a proxima falha (max 30 s).
        // O timer chama esp_wifi_connect() fora do contexto do event handler.
        uint64_t delay_us = (uint64_t)wifi_backoff_ms * 1000ULL;
        wifi_backoff_ms = (wifi_backoff_ms * 2 > WIFI_BACKOFF_MAX_MS)
                          ? WIFI_BACKOFF_MAX_MS
                          : wifi_backoff_ms * 2;

        if (wifi_timer_reconexao != NULL)
        {
            esp_timer_stop(wifi_timer_reconexao);
            esp_timer_start_once(wifi_timer_reconexao, delay_us);
        }

        // Sinaliza falha inicial ao main.c (para o mecanismo de backup WiFi) apenas uma vez.
        // CONTINUA tentando em background independentemente desta sinalizacao.
        if (wifi_tentativas >= WIFI_KINCONY_TENTATIVAS_FALHA_INICIAL && !wifi_falha)
        {
            wifi_falha = true;
            xEventGroupSetBits(wifi_event_group, WIFI_BIT_FALHA);
            ESP_LOGE(TAG, "WiFi: %u tentativas falhas - sinalizado ao main.c, backoff maximo %lu s",
                     wifi_tentativas, (unsigned long)(WIFI_BACKOFF_MAX_MS / 1000));
        }
        else
        {
            ESP_LOGW(TAG, "WiFi: desconectado (tentativa %u) - reconectando em %lu s",
                     wifi_tentativas, (unsigned long)(delay_us / 1000000ULL));
        }
    }

    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;

        wifi_tentativas = 0;
        wifi_conectado = true;
        wifi_falha = false;
        wifi_backoff_ms = WIFI_BACKOFF_MIN_MS; // reset para proxima eventual desconexao

        snprintf(wifi_ip_atual, sizeof(wifi_ip_atual), IPSTR, IP2STR(&event->ip_info.ip));

        xEventGroupSetBits(wifi_event_group, WIFI_BIT_CONECTADO);

        ESP_LOGI(TAG, "WiFi conectado. IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

esp_err_t Wifi_Kincony_Init(const char *ssid, const char *senha)
{
    if (ssid == NULL || senha == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    strncpy(wifi_ssid, ssid, sizeof(wifi_ssid) - 1);
    strncpy(wifi_senha, senha, sizeof(wifi_senha) - 1);

    wifi_conectado = false;
    wifi_falha = false;
    wifi_tentativas = 0;

    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Erro ao iniciar NVS");
        return ret;
    }

    wifi_event_group = xEventGroupCreate();

    if (wifi_event_group == NULL)
    {
        ESP_LOGE(TAG, "Erro ao criar event group do WiFi");
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();

    ret = esp_wifi_init(&wifi_init_config);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Erro ao inicializar WiFi");
        return ret;
    }

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        &Wifi_Kincony_EventHandler,
        NULL,
        NULL
    ));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT,
        IP_EVENT_STA_GOT_IP,
        &Wifi_Kincony_EventHandler,
        NULL,
        NULL
    ));

    wifi_config_t wifi_config = {0};

    strncpy((char *)wifi_config.sta.ssid, wifi_ssid, sizeof(wifi_config.sta.ssid));
    strncpy((char *)wifi_config.sta.password, wifi_senha, sizeof(wifi_config.sta.password));

    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    // Editado por Eraldo Bispo - 11/07/2026 - cria o timer de backoff antes de iniciar o WiFi
    // (o evento STA_DISCONNECTED pode disparar ja na primeira tentativa se o AP nao for encontrado).
    esp_timer_create_args_t timer_args = {
        .callback = wifi_reconectar_cb,
        .arg      = NULL,
        .name     = "wifi_backoff"
    };
    esp_timer_create(&timer_args, &wifi_timer_reconexao);

    wifi_backoff_ms = WIFI_BACKOFF_MIN_MS;

    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi iniciado. Conectando em: %s", wifi_ssid);

    return ESP_OK;
}

// Criado por Eraldo Bispo — usado pelo main.c para saber se deve reverter para o WiFi anterior em caso de falha
bool Wifi_Kincony_EsperarResultado(uint32_t timeout_ms)
{
    if (wifi_event_group == NULL)
    {
        return false;
    }

    EventBits_t bits = xEventGroupWaitBits(
        wifi_event_group,
        WIFI_BIT_CONECTADO | WIFI_BIT_FALHA,
        pdFALSE,
        pdFALSE,
        pdMS_TO_TICKS(timeout_ms)
    );

    return (bits & WIFI_BIT_CONECTADO) != 0;
}

bool Wifi_Kincony_IsConectado(void)
{
    return wifi_conectado;
}

bool Wifi_Kincony_IsFalha(void)
{
    return wifi_falha;
}

uint8_t Wifi_Kincony_GetTentativas(void)
{
    return wifi_tentativas;
}

void Wifi_Kincony_GetIpAtual(char *destino, size_t tamanho)
{
    if (destino == NULL || tamanho == 0)
    {
        return;
    }

    strncpy(destino, wifi_conectado ? wifi_ip_atual : "", tamanho - 1);
    destino[tamanho - 1] = '\0';
}

esp_err_t Wifi_Kincony_Reconectar(void)
{
    wifi_falha = false;
    wifi_conectado = false;
    wifi_tentativas = 0;
    wifi_backoff_ms = WIFI_BACKOFF_MIN_MS; // recomeça do backoff minimo

    if (wifi_event_group != NULL)
    {
        xEventGroupClearBits(wifi_event_group, WIFI_BIT_CONECTADO | WIFI_BIT_FALHA);
    }

    if (wifi_timer_reconexao != NULL)
    {
        esp_timer_stop(wifi_timer_reconexao);
    }

    return esp_wifi_connect();
}

// Criado por Eraldo Bispo — SSID/senha do Access Point de emergencia. Fixos no codigo (e nao no
// painel/NVS) de proposito: se o ESP nao consegue nem mostrar o painel, nao tem como configurar isso.
#define WIFI_KINCONY_AP_SSID    "Kincony-Config"
#define WIFI_KINCONY_AP_SENHA   "kincony2026"

esp_err_t Wifi_Kincony_IniciarModoEmergenciaAP(void)
{
    esp_netif_create_default_wifi_ap();

    wifi_config_t ap_config = {0};

    strncpy((char *)ap_config.ap.ssid, WIFI_KINCONY_AP_SSID, sizeof(ap_config.ap.ssid));
    ap_config.ap.ssid_len = strlen(WIFI_KINCONY_AP_SSID);
    strncpy((char *)ap_config.ap.password, WIFI_KINCONY_AP_SENHA, sizeof(ap_config.ap.password));
    ap_config.ap.max_connection = 4;
    ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;

    // Editado por Eraldo Bispo — APSTA (nao so AP): mantem a tentativa de reconexao STA em segundo
    // plano, para o ESP voltar a usar a rede normal automaticamente se ela aparecer de novo
    esp_err_t ret = esp_wifi_set_mode(WIFI_MODE_APSTA);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Erro ao ativar modo AP de emergencia");
        return ret;
    }

    ret = esp_wifi_set_config(WIFI_IF_AP, &ap_config);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Erro ao configurar AP de emergencia");
        return ret;
    }

    ESP_LOGW(TAG, "Nenhum WiFi conhecido encontrado. Modo de emergencia ativado:");
    ESP_LOGW(TAG, "Conecte na rede '%s' (senha: %s) e acesse http://192.168.4.1", WIFI_KINCONY_AP_SSID, WIFI_KINCONY_AP_SENHA);

    return ESP_OK;
}

esp_err_t Wifi_Kincony_Desconectar(void)
{
    wifi_conectado = false;
    wifi_ip_atual[0] = '\0';

    if (wifi_event_group != NULL)
    {
        xEventGroupClearBits(wifi_event_group, WIFI_BIT_CONECTADO);
    }

    return esp_wifi_disconnect();
}