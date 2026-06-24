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

#include <string.h>

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "rtc/rtc_ds1307.h"

#define TAG "WIFI_KINCONY"

#define WIFI_BIT_CONECTADO     BIT0
#define WIFI_BIT_FALHA         BIT1

static EventGroupHandle_t wifi_event_group = NULL;

static bool wifi_conectado = false;
static bool wifi_falha = false;
static uint8_t wifi_tentativas = 0;

static char wifi_ssid[32] = {0};
static char wifi_senha[64] = {0};

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

        if (wifi_tentativas < WIFI_KINCONY_TENTATIVAS_MAX)
        {
            wifi_tentativas++;
            esp_wifi_connect();

            ESP_LOGW(TAG, "Tentando reconectar WiFi... tentativa %d", wifi_tentativas);
        }
        else
        {
            wifi_falha = true;
            xEventGroupSetBits(wifi_event_group, WIFI_BIT_FALHA);

            ESP_LOGE(TAG, "Falha ao conectar WiFi");
        }
    }

    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;

        wifi_tentativas = 0;
        wifi_conectado = true;
        wifi_falha = false;

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
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi iniciado. Conectando em: %s", wifi_ssid);

    RTC_DS1307_AtualizarHorarioInternet();

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

esp_err_t Wifi_Kincony_Reconectar(void)
{
    wifi_falha = false;
    wifi_conectado = false;
    wifi_tentativas = 0;

    if (wifi_event_group != NULL)
    {
        xEventGroupClearBits(wifi_event_group, WIFI_BIT_CONECTADO | WIFI_BIT_FALHA);
    }

    return esp_wifi_connect();
}

esp_err_t Wifi_Kincony_Desconectar(void)
{
    wifi_conectado = false;

    if (wifi_event_group != NULL)
    {
        xEventGroupClearBits(wifi_event_group, WIFI_BIT_CONECTADO);
    }

    return esp_wifi_disconnect();
}