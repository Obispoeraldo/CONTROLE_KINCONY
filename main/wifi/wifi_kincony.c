/*
 * wifi_kincony.c
 *
 * Driver de conexao WiFi do firmware Kincony - maquina de estados nao bloqueante.
 * Ver diagnostico e arquitetura completa em docs/RELATORIO_WIFI_RECONEXAO.md.
 *
 * Historico: v1 (DANIEL, 2024) usava esp_wifi_connect() direto no evento STA_START e
 * dependia so do driver WiFi para reconectar, sem portal acessivel durante as tentativas.
 * v1.1 (Eraldo Bispo, 11/07/2026) adicionou backoff exponencial (1s-30s) via esp_timer.
 * v2 (Eraldo Bispo) - esta versao - adiciona: intervalo progressivo com teto maior
 * (30s/60s/5min), portal de configuracao independente das tentativas (APSTA sob
 * demanda, sem recriar a interface AP a cada abertura), abertura automatica do portal
 * apos desconexao prolongada, e teste de rede nova sem gravar em NVS.
 */

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
#include "freertos/task.h"

#define TAG "WIFI_KINCONY"

#define WIFI_BIT_CONECTADO     BIT0
#define WIFI_BIT_FALHA         BIT1

// Editado por Eraldo Bispo - intervalo entre tentativas de reconexao (nao bloqueante - so
// controla quando o esp_timer dispara o proximo esp_wifi_connect()). Escada em vez de
// exponencial puro, para ficar previsivel em log/manutencao: 30s nas primeiras tentativas,
// 60s depois de varias falhas, 5 min depois de um periodo prolongado (teto). O router pode
// levar 30-120s pra voltar apos queda de energia; a rede nunca e abandonada definitivamente.
#define WIFI_RETRY_INTERVALO_INICIAL_MS     (30U * 1000U)
#define WIFI_RETRY_INTERVALO_MEDIO_MS       (60U * 1000U)
#define WIFI_RETRY_INTERVALO_MAXIMO_MS      (5U * 60U * 1000U)
#define WIFI_RETRY_TENTATIVAS_ANTES_MEDIO   5U
#define WIFI_RETRY_TENTATIVAS_ANTES_MAXIMO  10U

// Criado por Eraldo Bispo - se ficar desconectado continuamente por mais que isto, abre o
// portal de configuracao sozinho (alem do botao fisico, que abre a qualquer momento) - ver
// Wifi_Kincony_Processar(). Fecha sozinho quando a rede salva reconectar (se foi aberto
// automaticamente - portal aberto manualmente so fecha por acao explicita).
#define WIFI_KINCONY_AUTO_AP_LIMIAR_MS      (2U * 60U * 1000U)

static EventGroupHandle_t wifi_event_group = NULL;

static bool wifi_conectado = false;
static bool wifi_falha = false;
static uint8_t wifi_tentativas = 0;
static esp_timer_handle_t wifi_timer_reconexao = NULL;
static wifi_kincony_estado_t wifi_estado = WIFI_KINCONY_ESTADO_SEM_CREDENCIAIS;

static char wifi_ssid[32] = {0};
static char wifi_senha[64] = {0};

// Criado por Eraldo Bispo - IP atual da interface STA, atualizado no evento IP_EVENT_STA_GOT_IP
// e limpo na desconexao, para Wifi_Kincony_GetIpAtual() expor sem consultar a interface direto.
static char wifi_ip_atual[16] = {0};

// ---- Portal de configuracao (AP) ----
#define WIFI_KINCONY_AP_SSID    "Kincony-Config"
#define WIFI_KINCONY_AP_SENHA   "kincony2026"

static bool s_ap_ativo = false;
static bool s_ap_manual = false;
static int64_t s_desconectado_desde_us = 0; // 0 = nao aplicavel (conectado ou ainda conectando)

// ---- Teste de rede nova (Wifi_Kincony_TestarNovaRede) ----
static volatile bool s_testando_rede = false;

static uint32_t wifi_calcular_intervalo_retry(uint8_t tentativas)
{
    if (tentativas <= WIFI_RETRY_TENTATIVAS_ANTES_MEDIO)
    {
        return WIFI_RETRY_INTERVALO_INICIAL_MS;
    }

    if (tentativas <= WIFI_RETRY_TENTATIVAS_ANTES_MAXIMO)
    {
        return WIFI_RETRY_INTERVALO_MEDIO_MS;
    }

    return WIFI_RETRY_INTERVALO_MAXIMO_MS;
}

// Chamado pelo esp_timer apos o intervalo de retry: tenta reconectar sem bloquear nenhuma task.
static void wifi_reconectar_cb(void *arg)
{
    ESP_LOGW(TAG, "WiFi: tentando reconectar...");
    esp_wifi_connect();
}

static void wifi_aplicar_config_sta(const char *ssid, const char *senha)
{
    wifi_config_t cfg = {0};

    strncpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid) - 1);
    strncpy((char *)cfg.sta.password, senha, sizeof(cfg.sta.password) - 1);
    cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    esp_wifi_set_config(WIFI_IF_STA, &cfg);

    strncpy(wifi_ssid, ssid, sizeof(wifi_ssid) - 1);
    wifi_ssid[sizeof(wifi_ssid) - 1] = '\0';
    strncpy(wifi_senha, senha, sizeof(wifi_senha) - 1);
    wifi_senha[sizeof(wifi_senha) - 1] = '\0';
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
        wifi_estado = WIFI_KINCONY_ESTADO_AGUARDANDO_CONEXAO;
        esp_wifi_connect();
    }

    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        wifi_conectado = false;
        wifi_ip_atual[0] = '\0';

        // Wifi_Kincony_TestarNovaRede() esta aguardando este evento diretamente (via
        // WIFI_BIT_FALHA) - nao mexe em tentativas/temporizador/estado enquanto o teste
        // estiver rodando, quem trata o resultado e a propria funcao de teste.
        if (s_testando_rede)
        {
            xEventGroupSetBits(wifi_event_group, WIFI_BIT_FALHA);
            return;
        }

        if (s_desconectado_desde_us == 0)
        {
            s_desconectado_desde_us = esp_timer_get_time();
        }

        if (wifi_tentativas < UINT8_MAX)
        {
            wifi_tentativas++;
        }

        uint32_t intervalo_ms = wifi_calcular_intervalo_retry(wifi_tentativas);

        wifi_estado = WIFI_KINCONY_ESTADO_AGUARDANDO_NOVA_TENTATIVA;

        if (wifi_timer_reconexao != NULL)
        {
            esp_timer_stop(wifi_timer_reconexao);
            esp_timer_start_once(wifi_timer_reconexao, (uint64_t)intervalo_ms * 1000ULL);
        }

        // Sinaliza falha inicial ao main.c (para o mecanismo de restaurar backup no boot)
        // apenas uma vez. CONTINUA tentando em segundo plano independente desta sinalizacao.
        if (wifi_tentativas >= WIFI_KINCONY_TENTATIVAS_FALHA_INICIAL && !wifi_falha)
        {
            wifi_falha = true;
            xEventGroupSetBits(wifi_event_group, WIFI_BIT_FALHA);
            ESP_LOGE(TAG, "WiFi: %u tentativas falhas - sinalizado ao main.c, proximo intervalo %lu s",
                     wifi_tentativas, (unsigned long)(intervalo_ms / 1000));
        }
        else
        {
            ESP_LOGW(TAG, "WiFi: desconectado (tentativa %u) - reconectando em %lu s",
                     wifi_tentativas, (unsigned long)(intervalo_ms / 1000));
        }
    }

    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;

        wifi_tentativas = 0;
        wifi_conectado = true;
        wifi_falha = false;
        wifi_estado = WIFI_KINCONY_ESTADO_CONECTADO;
        s_desconectado_desde_us = 0;

        snprintf(wifi_ip_atual, sizeof(wifi_ip_atual), IPSTR, IP2STR(&event->ip_info.ip));

        xEventGroupSetBits(wifi_event_group, WIFI_BIT_CONECTADO);

        ESP_LOGI(TAG, "WiFi conectado. IP: " IPSTR, IP2STR(&event->ip_info.ip));

        // Portal aberto automaticamente por desconexao prolongada: agora que a rede salva
        // voltou, fecha sozinho (menos exposicao/consumo). Portal aberto manualmente (botao
        // fisico ou painel) so fecha por acao explicita do operador.
        if (s_ap_ativo && !s_ap_manual)
        {
            ESP_LOGI(TAG, "Rede reconectada - fechando portal de configuracao aberto automaticamente.");
            Wifi_Kincony_FecharPortalConfiguracao();
        }
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
    s_desconectado_desde_us = 0;
    wifi_estado = (strlen(wifi_ssid) == 0)
                  ? WIFI_KINCONY_ESTADO_SEM_CREDENCIAIS
                  : WIFI_KINCONY_ESTADO_INICIANDO_CONEXAO;

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
    // Criado por Eraldo Bispo - interface AP criada uma UNICA vez aqui (nao a cada abertura
    // do portal), para nunca "criar Access Point repetidamente" - so alternamos o modo WiFi
    // entre STA e APSTA em Wifi_Kincony_AbrirPortalConfiguracao()/FecharPortalConfiguracao().
    esp_netif_create_default_wifi_ap();

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

    wifi_config_t ap_config = {0};
    strncpy((char *)ap_config.ap.ssid, WIFI_KINCONY_AP_SSID, sizeof(ap_config.ap.ssid) - 1);
    ap_config.ap.ssid_len = strlen(WIFI_KINCONY_AP_SSID);
    strncpy((char *)ap_config.ap.password, WIFI_KINCONY_AP_SENHA, sizeof(ap_config.ap.password) - 1);
    ap_config.ap.max_connection = 4;
    ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;

    wifi_config_t sta_config = {0};
    strncpy((char *)sta_config.sta.ssid, wifi_ssid, sizeof(sta_config.sta.ssid) - 1);
    strncpy((char *)sta_config.sta.password, wifi_senha, sizeof(sta_config.sta.password) - 1);
    sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    // Configura AP e STA em modo APSTA (necessario para o driver aceitar esp_wifi_set_config
    // em ambas as interfaces), depois volta para STA-only: o AP fica configurado e pronto,
    // mas inativo, ate Wifi_Kincony_AbrirPortalConfiguracao() ligar o modo APSTA de novo -
    // sem precisar reconfigurar nem recriar a interface.
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    esp_timer_create_args_t timer_args = {
        .callback = wifi_reconectar_cb,
        .arg      = NULL,
        .name     = "wifi_retry"
    };
    esp_timer_create(&timer_args, &wifi_timer_reconexao);

    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi iniciado. Conectando em: %s", wifi_ssid);

    return ESP_OK;
}

void Wifi_Kincony_Processar(void)
{
    // Abertura automatica do portal apos desconexao prolongada (alem do botao fisico, que
    // abre a qualquer momento - ver wifi_config_button.c). So dispara uma vez por periodo de
    // desconexao (s_ap_ativo evita reabrir), e so se ninguem ja abriu manualmente.
    if (!wifi_conectado && !s_ap_ativo && s_desconectado_desde_us != 0)
    {
        int64_t decorrido_us = esp_timer_get_time() - s_desconectado_desde_us;

        if (decorrido_us >= (int64_t)WIFI_KINCONY_AUTO_AP_LIMIAR_MS * 1000)
        {
            ESP_LOGW(TAG, "Desconectado ha mais de %lu min - abrindo portal de configuracao automaticamente.",
                     (unsigned long)(WIFI_KINCONY_AUTO_AP_LIMIAR_MS / 60000U));
            Wifi_Kincony_AbrirPortalConfiguracao(false);
        }
    }
}

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

wifi_kincony_estado_t Wifi_Kincony_GetEstado(void)
{
    if (s_ap_ativo)
    {
        return WIFI_KINCONY_ESTADO_PORTAL_CONFIGURACAO_ATIVO;
    }

    return wifi_estado;
}

const char *Wifi_Kincony_EstadoToString(wifi_kincony_estado_t estado)
{
    switch (estado)
    {
        case WIFI_KINCONY_ESTADO_SEM_CREDENCIAIS:            return "SEM_CREDENCIAIS";
        case WIFI_KINCONY_ESTADO_INICIANDO_CONEXAO:          return "INICIANDO_CONEXAO";
        case WIFI_KINCONY_ESTADO_AGUARDANDO_CONEXAO:         return "AGUARDANDO_CONEXAO";
        case WIFI_KINCONY_ESTADO_CONECTADO:                  return "CONECTADO";
        case WIFI_KINCONY_ESTADO_DESCONECTADO:               return "DESCONECTADO";
        case WIFI_KINCONY_ESTADO_AGUARDANDO_NOVA_TENTATIVA:  return "AGUARDANDO_NOVA_TENTATIVA";
        case WIFI_KINCONY_ESTADO_PORTAL_CONFIGURACAO_ATIVO:  return "PORTAL_CONFIGURACAO_ATIVO";
        case WIFI_KINCONY_ESTADO_VALIDANDO_NOVA_REDE:        return "VALIDANDO_NOVA_REDE";
        case WIFI_KINCONY_ESTADO_ERRO_NOVA_REDE:             return "ERRO_NOVA_REDE";
        default:                                             return "DESCONHECIDO";
    }
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
    s_desconectado_desde_us = 0;

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

esp_err_t Wifi_Kincony_AbrirPortalConfiguracao(bool manual)
{
    if (manual)
    {
        s_ap_manual = true;
    }

    if (s_ap_ativo)
    {
        // Idempotente - ja esta aberto. So promove para "manual" se for o caso (impede que o
        // fechamento automatico feche um portal que o operador abriu de proposito).
        return ESP_OK;
    }

    esp_err_t ret = esp_wifi_set_mode(WIFI_MODE_APSTA);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Erro ao abrir portal de configuracao: %s", esp_err_to_name(ret));
        return ret;
    }

    s_ap_ativo = true;

    ESP_LOGW(TAG, "Portal de configuracao ABERTO. Rede '%s' (senha: %s) - acesse http://192.168.4.1",
             WIFI_KINCONY_AP_SSID, WIFI_KINCONY_AP_SENHA);

    return ESP_OK;
}

esp_err_t Wifi_Kincony_FecharPortalConfiguracao(void)
{
    if (!s_ap_ativo)
    {
        return ESP_OK;
    }

    esp_err_t ret = esp_wifi_set_mode(WIFI_MODE_STA);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Erro ao fechar portal de configuracao: %s", esp_err_to_name(ret));
        return ret;
    }

    s_ap_ativo = false;
    s_ap_manual = false;

    ESP_LOGI(TAG, "Portal de configuracao fechado.");

    return ESP_OK;
}

bool Wifi_Kincony_PortalAtivo(void)
{
    return s_ap_ativo;
}

esp_err_t Wifi_Kincony_IniciarModoEmergenciaAP(void)
{
    return Wifi_Kincony_AbrirPortalConfiguracao(true);
}

bool Wifi_Kincony_TestarNovaRede(const char *ssid, const char *senha, uint32_t timeout_ms)
{
    if (ssid == NULL || senha == NULL || wifi_event_group == NULL)
    {
        return false;
    }

    char ssid_anterior[32];
    char senha_anterior[64];
    strncpy(ssid_anterior, wifi_ssid, sizeof(ssid_anterior));
    strncpy(senha_anterior, wifi_senha, sizeof(senha_anterior));

    ESP_LOGI(TAG, "Testando rede nova '%s' (rede atual '%s' preservada ate confirmar)...", ssid, ssid_anterior);

    wifi_estado = WIFI_KINCONY_ESTADO_VALIDANDO_NOVA_REDE;
    s_testando_rede = true;

    // Impede que o retry em segundo plano dispare esp_wifi_connect() com a config antiga
    // enquanto o teste estiver rodando.
    if (wifi_timer_reconexao != NULL)
    {
        esp_timer_stop(wifi_timer_reconexao);
    }

    xEventGroupClearBits(wifi_event_group, WIFI_BIT_CONECTADO | WIFI_BIT_FALHA);

    // Desconectar da rede ANTIGA aqui gera seu proprio WIFI_EVENT_STA_DISCONNECTED (assincrono).
    // Como s_testando_rede ja esta true, o handler acima seta WIFI_BIT_FALHA para esse evento -
    // sem esta espera, esse desconectar esperado da rede antiga era contado como se fosse a rede
    // NOVA falhando (teste "falhava" em ~30ms, antes de sequer tentar a rede nova de verdade).
    // Da um tempo curto para esse desconectar se resolver e limpa o bit de novo logo antes de
    // conectar na rede nova - só a partir daqui um WIFI_BIT_FALHA representa a rede nova.
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(300));
    xEventGroupClearBits(wifi_event_group, WIFI_BIT_FALHA);

    wifi_aplicar_config_sta(ssid, senha);
    esp_wifi_connect();

    EventBits_t bits = xEventGroupWaitBits(
        wifi_event_group,
        WIFI_BIT_CONECTADO | WIFI_BIT_FALHA,
        pdFALSE,
        pdFALSE,
        pdMS_TO_TICKS(timeout_ms)
    );

    bool sucesso = (bits & WIFI_BIT_CONECTADO) != 0;

    s_testando_rede = false;

    if (!sucesso)
    {
        ESP_LOGW(TAG, "Teste da rede '%s' falhou - restaurando '%s'.", ssid, ssid_anterior);

        wifi_estado = WIFI_KINCONY_ESTADO_ERRO_NOVA_REDE;

        esp_wifi_disconnect();
        wifi_aplicar_config_sta(ssid_anterior, senha_anterior);

        wifi_tentativas = 0;
        s_desconectado_desde_us = 0;
        xEventGroupClearBits(wifi_event_group, WIFI_BIT_CONECTADO | WIFI_BIT_FALHA);

        esp_wifi_connect();
    }
    else
    {
        ESP_LOGI(TAG, "Teste da rede '%s' OK - conexao confirmada.", ssid);
    }

    return sucesso;
}
