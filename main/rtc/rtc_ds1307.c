/*
 * rtc_ds1307.c
 *
 * DS1307 + sincronizacao NTP + 4 timers persistentes via NVS.
 *
 * IMPORTANTE:
 * - Este modulo NAO configura SDA/SCL;
 * - Este modulo NAO instala o driver I2C;
 * - O I2C_NUM_0 deve estar iniciado antes de RTC_DS1307_Iniciar().
 */

#include "rtc_ds1307.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "cJSON.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"

#include "logica_controle.h"
// Editado por Eraldo Bispo e Daniel Montanher - integracao - RTC_DS1307_NotificarSincronizacaoNtp()
// e o gatilho de resync manual usam o cliente SNTP unico de ntp_kincony.c (esp_netif_sntp), em vez
// de o RTC manter seu proprio cliente SNTP concorrente. Ver secao "Servico de tempo" do relatorio
// de integracao (docs/RELATORIO_INTEGRACAO_ERALDO_DANIEL.md).
#include "ntp_kincony.h"

#define TAG "RTC_DS1307"

#define I2C_PORT                         I2C_NUM_0
#define DS1307_ADDR                      0x68

#define RTC_TIMER_QUANTIDADE             4
#define RTC_TIMER_NVS_NAMESPACE          "rtc_timers"
#define RTC_TIMER_NVS_KEY                "config"
#define RTC_TIMER_STORAGE_MAGIC          0x52544354UL
#define RTC_TIMER_STORAGE_VERSION        1

#define RTC_PROCESSAMENTO_INTERVALO_MS   1000

typedef struct
{
    uint8_t configurado;
    uint8_t habilitado;
    uint8_t grupo;
    uint8_t hora_liga;
    uint8_t minuto_liga;
    uint8_t hora_desliga;
    uint8_t minuto_desliga;
    uint8_t reservado;
} rtc_timer_config_t;

typedef struct
{
    uint32_t magic;
    uint8_t version;
    uint8_t reservado[3];
    rtc_timer_config_t timers[RTC_TIMER_QUANTIDADE];
} rtc_timer_storage_t;

static rtc_timer_config_t s_timers[RTC_TIMER_QUANTIDADE];

static SemaphoreHandle_t s_mutex = NULL;

static volatile bool s_ntp_sincronizado = false;
// Editado por Eraldo Bispo e Daniel Montanher - integracao - evita chamadas repetidas ao I2C
// (e log flood) quando o chip DS1307 nao respondeu no boot (RTC_DS1307_Iniciar()).
static volatile bool s_rtc_disponivel = false;

static TickType_t s_ultimo_processamento = 0;
static uint32_t s_ultima_assinatura_minuto = UINT32_MAX;

static uint8_t dec_para_bcd(uint8_t valor)
{
    return (uint8_t)(((valor / 10U) << 4U) | (valor % 10U));
}

static uint8_t bcd_para_dec(uint8_t valor)
{
    return (uint8_t)(((valor >> 4U) * 10U) + (valor & 0x0FU));
}

static bool ano_bissexto(uint16_t ano)
{
    return ((ano % 400U) == 0U) ||
           (((ano % 4U) == 0U) && ((ano % 100U) != 0U));
}

static uint8_t dias_no_mes(uint8_t mes, uint16_t ano)
{
    static const uint8_t dias[] = {
        31, 28, 31, 30, 31, 30,
        31, 31, 30, 31, 30, 31
    };

    if (mes < 1U || mes > 12U)
    {
        return 0;
    }

    if (mes == 2U && ano_bissexto(ano))
    {
        return 29;
    }

    return dias[mes - 1U];
}

static bool rtc_horario_valido(const rtc_ds1307_t *rtc)
{
    if (rtc == NULL)
    {
        return false;
    }

    if (rtc->ano < 2024U || rtc->ano > 2099U)
    {
        return false;
    }

    if (rtc->mes < 1U || rtc->mes > 12U)
    {
        return false;
    }

    if (rtc->dia < 1U || rtc->dia > dias_no_mes(rtc->mes, rtc->ano))
    {
        return false;
    }

    if (rtc->dia_semana < 1U || rtc->dia_semana > 7U)
    {
        return false;
    }

    if (rtc->hora > 23U || rtc->minuto > 59U || rtc->segundo > 59U)
    {
        return false;
    }

    return true;
}

static esp_err_t rtc_aplicar_no_relogio_sistema(const rtc_ds1307_t *rtc)
{
    if (!rtc_horario_valido(rtc))
    {
        return ESP_ERR_INVALID_ARG;
    }

    setenv("TZ", "BRT3", 1);
    tzset();

    struct tm horario = {
        .tm_sec = rtc->segundo,
        .tm_min = rtc->minuto,
        .tm_hour = rtc->hora,
        .tm_mday = rtc->dia,
        .tm_mon = rtc->mes - 1,
        .tm_year = rtc->ano - 1900,
        .tm_isdst = -1
    };

    time_t epoch = mktime(&horario);

    if (epoch == (time_t)-1)
    {
        return ESP_FAIL;
    }

    struct timeval tv = {
        .tv_sec = epoch,
        .tv_usec = 0
    };

    if (settimeofday(&tv, NULL) != 0)
    {
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t timer_salvar_nvs(void)
{
    rtc_timer_storage_t storage = {
        .magic = RTC_TIMER_STORAGE_MAGIC,
        .version = RTC_TIMER_STORAGE_VERSION
    };

    memcpy(storage.timers, s_timers, sizeof(s_timers));

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(
        RTC_TIMER_NVS_NAMESPACE,
        NVS_READWRITE,
        &handle
    );

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Erro ao abrir NVS dos timers: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_set_blob(
        handle,
        RTC_TIMER_NVS_KEY,
        &storage,
        sizeof(storage)
    );

    if (ret == ESP_OK)
    {
        ret = nvs_commit(handle);
    }

    nvs_close(handle);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Erro ao salvar timers: %s", esp_err_to_name(ret));
    }

    return ret;
}

static esp_err_t timer_carregar_nvs(void)
{
    memset(s_timers, 0, sizeof(s_timers));

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(
        RTC_TIMER_NVS_NAMESPACE,
        NVS_READONLY,
        &handle
    );

    if (ret == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGI(TAG, "Nenhum timer salvo na NVS");
        return ESP_OK;
    }

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Erro ao abrir NVS dos timers: %s", esp_err_to_name(ret));
        return ret;
    }

    rtc_timer_storage_t storage;
    size_t tamanho = sizeof(storage);

    ret = nvs_get_blob(
        handle,
        RTC_TIMER_NVS_KEY,
        &storage,
        &tamanho
    );

    nvs_close(handle);

    if (ret == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGI(TAG, "Nenhum timer salvo na NVS");
        return ESP_OK;
    }

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Erro ao carregar timers: %s", esp_err_to_name(ret));
        return ret;
    }

    if (tamanho != sizeof(storage) ||
        storage.magic != RTC_TIMER_STORAGE_MAGIC ||
        storage.version != RTC_TIMER_STORAGE_VERSION)
    {
        ESP_LOGW(TAG, "Configuracao de timers invalida ou de outra versao");
        memset(s_timers, 0, sizeof(s_timers));
        return ESP_OK;
    }

    memcpy(s_timers, storage.timers, sizeof(s_timers));

    ESP_LOGI(TAG, "Timers carregados da NVS");
    return ESP_OK;
}

static bool horario_texto_valido(
    const char *texto,
    uint8_t *hora,
    uint8_t *minuto
)
{
    if (texto == NULL || hora == NULL || minuto == NULL)
    {
        return false;
    }

    unsigned int h = 0;
    unsigned int m = 0;
    char sobra = '\0';

    if (sscanf(texto, "%2u:%2u%c", &h, &m, &sobra) != 2)
    {
        return false;
    }

    if (strlen(texto) != 5U || texto[2] != ':')
    {
        return false;
    }

    if (h > 23U || m > 59U)
    {
        return false;
    }

    *hora = (uint8_t)h;
    *minuto = (uint8_t)m;

    return true;
}


static void formatar_hhmm(char destino[6], uint8_t hora, uint8_t minuto)
{
    destino[0] = (char)('0' + (hora / 10U));
    destino[1] = (char)('0' + (hora % 10U));
    destino[2] = ':';
    destino[3] = (char)('0' + (minuto / 10U));
    destino[4] = (char)('0' + (minuto % 10U));
    destino[5] = '\0';
}

static bool json_obter_enabled(const cJSON *item, bool *valor)
{
    if (item == NULL || valor == NULL)
    {
        return false;
    }

    if (cJSON_IsBool(item))
    {
        *valor = cJSON_IsTrue(item);
        return true;
    }

    if (cJSON_IsNumber(item) &&
        (item->valueint == 0 || item->valueint == 1))
    {
        *valor = (item->valueint == 1);
        return true;
    }

    return false;
}

static esp_err_t json_finalizar(
    cJSON *objeto,
    char *resposta,
    size_t tamanho_resposta
)
{
    if (objeto == NULL || resposta == NULL || tamanho_resposta == 0U)
    {
        cJSON_Delete(objeto);
        return ESP_ERR_INVALID_ARG;
    }

    bool ok = cJSON_PrintPreallocated(
        objeto,
        resposta,
        (int)tamanho_resposta,
        false
    );

    cJSON_Delete(objeto);

    if (!ok)
    {
        resposta[0] = '\0';
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

static esp_err_t json_resposta_simples(
    const char *acao,
    const char *resultado,
    int id,
    char *resposta,
    size_t tamanho_resposta
)
{
    cJSON *root = cJSON_CreateObject();

    if (root == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(root, "action", acao);
    cJSON_AddStringToObject(root, "result", resultado);

    if (id > 0)
    {
        cJSON_AddNumberToObject(root, "id", id);
    }

    return json_finalizar(root, resposta, tamanho_resposta);
}

static void rtc_adicionar_horario_json(cJSON *root)
{
    rtc_ds1307_t rtc;
    esp_err_t ret = RTC_DS1307_LerHorario(&rtc);

    cJSON_AddBoolToObject(root, "rtc_online", ret == ESP_OK);
    cJSON_AddBoolToObject(root, "ntp_synced", s_ntp_sincronizado);

    if (ret == ESP_OK)
    {
        char agora[32] = {0};

        struct tm horario = {
            .tm_sec = rtc.segundo,
            .tm_min = rtc.minuto,
            .tm_hour = rtc.hora,
            .tm_mday = rtc.dia,
            .tm_mon = rtc.mes - 1,
            .tm_year = rtc.ano - 1900,
            .tm_isdst = -1
        };

        strftime(
            agora,
            sizeof(agora),
            "%Y-%m-%d %H:%M:%S",
            &horario
        );

        cJSON_AddStringToObject(root, "clock_source",
                                s_ntp_sincronizado ? "ntp" : "ds1307");
        cJSON_AddStringToObject(root, "now", agora);
    }
    else
    {
        cJSON_AddStringToObject(root, "clock_source", "invalid");
        cJSON_AddStringToObject(root, "now", "");
    }
}

static uint32_t rtc_assinatura_minuto(const rtc_ds1307_t *rtc)
{
    uint32_t assinatura = rtc->ano;

    assinatura = assinatura * 13U + rtc->mes;
    assinatura = assinatura * 32U + rtc->dia;
    assinatura = assinatura * 24U + rtc->hora;
    assinatura = assinatura * 60U + rtc->minuto;

    return assinatura;
}

/*
 * Regra central de propriedade (ver docs/RELATORIO_TIMER_PROPRIEDADE.md): um timer so pode
 * desligar um grupo se aquela execucao do timer tiver ligado o grupo e ainda mantiver a
 * propriedade dele. Qualquer comando aceito com origem != ORIGEM_TIMER (MQTT, manual local, etc.)
 * cancela a propriedade automaticamente em Logica_Controle_SetComandoGrupo() - aqui so precisamos
 * checar/gravar quem e o dono antes de agir, nunca sobrescrever um grupo que ja tem outra origem.
 *
 * timer_id identifica QUAL dos RTC_TIMER_QUANTIDADE timers configurados disparou o evento -
 * necessario para o caso de dois timers se sobrepondo (o timer que NAO ligou o grupo nunca pode
 * desliga-lo nem tomar a propriedade so porque o encontrou ligado).
 */
static void timer_executar_grupo(uint8_t timer_id, uint8_t grupo, bool ligar)
{
    if (grupo < 1U || grupo > LOGICA_CONTROLE_NUM_GRUPOS)
    {
        return;
    }

    logica_grupo_t idx = (logica_grupo_t)(grupo - 1U);

    if (ligar)
    {
        if (Logica_Controle_GetComandoGrupo(idx))
        {
            /* Grupo ja ligado - so o timer que e o dono atual pode ser "responsavel" por ele; se
             * pertence a outra origem (mqtt/manual/web) ou a outro timer, nao mexe em nada. */
            origem_comando_t origem_atual = Logica_Controle_GetOrigemGrupo(idx);

            if (origem_atual != ORIGEM_TIMER)
            {
                ESP_LOGI(TAG, "Timer %u: grupo %u ignorado - ja ligado por origem %s",
                         (unsigned int)timer_id, (unsigned int)grupo,
                         Logica_Controle_OrigemToMqttString(origem_atual));
            }
            else if (!Logica_Controle_TimerEhDono(idx, (int8_t)timer_id))
            {
                ESP_LOGI(TAG, "Timer %u: grupo %u ja pertence a outro timer, ignorado",
                         (unsigned int)timer_id, (unsigned int)grupo);
            }

            return;
        }

        esp_err_t ret = Logica_Controle_SetComandoGrupo(idx, true, ORIGEM_TIMER);

        if (ret == ESP_ERR_INVALID_STATE)
        {
            ESP_LOGW(TAG, "Timer ignorado: painel em modo local (grupo %u)", (unsigned int)grupo);
            return;
        }

        if (ret == ESP_OK)
        {
            Logica_Controle_TimerAssumirPropriedade(idx, (int8_t)timer_id);
            ESP_LOGI(TAG, "Timer %u: grupo %u acionado e registrado na execucao do timer",
                     (unsigned int)timer_id, (unsigned int)grupo);
        }

        return;
    }

    /* Desligar: so pode agir se esta execucao do timer ainda for a dona do grupo. */
    if (!Logica_Controle_TimerEhDono(idx, (int8_t)timer_id))
    {
        ESP_LOGI(TAG, "Timer %u: grupo %u preservado - nao pertence a este timer",
                 (unsigned int)timer_id, (unsigned int)grupo);
        return;
    }

    esp_err_t ret = Logica_Controle_SetComandoGrupo(idx, false, ORIGEM_TIMER);

    if (ret == ESP_ERR_INVALID_STATE)
    {
        ESP_LOGW(TAG, "Timer ignorado: painel em modo local (grupo %u)", (unsigned int)grupo);
        return;
    }

    Logica_Controle_TimerLiberarPropriedade(idx);
    ESP_LOGI(TAG, "Timer %u: grupo %u desligado ao finalizar timer",
             (unsigned int)timer_id, (unsigned int)grupo);
}

static void timer_executar(uint8_t timer_id, const rtc_timer_config_t *timer, bool ligar)
{
    if (timer == NULL)
    {
        return;
    }

    if (timer->grupo == 0U)
    {
        for (uint8_t grupo = 1U;
             grupo <= LOGICA_CONTROLE_NUM_GRUPOS;
             grupo++)
        {
            timer_executar_grupo(timer_id, grupo, ligar);
        }
    }
    else
    {
        timer_executar_grupo(timer_id, timer->grupo, ligar);
    }
}

esp_err_t RTC_DS1307_Iniciar(void)
{
    if (s_mutex == NULL)
    {
        s_mutex = xSemaphoreCreateMutex();

        if (s_mutex == NULL)
        {
            ESP_LOGE(TAG, "Falha ao criar mutex");
            return ESP_ERR_NO_MEM;
        }
    }

    uint8_t reg = 0x00;
    uint8_t valor = 0;

    esp_err_t ret = i2c_master_write_read_device(
        I2C_PORT,
        DS1307_ADDR,
        &reg,
        1,
        &valor,
        1,
        pdMS_TO_TICKS(100)
    );

    if (ret != ESP_OK)
    {
        // Editado por Eraldo Bispo e Daniel Montanher - integracao - nao propaga mais o erro
        // como fatal: RTC_DS1307_Iniciar() e chamado sem ESP_ERROR_CHECK em main.c de proposito.
        // Placas/variantes sem o chip DS1307 fisico (ver nota em ntp_kincony.h) devem continuar
        // operando normalmente com a hora vindo apenas do NTP; os 4 timers de horario ficam
        // apenas indisponiveis, sem travar o boot do controlador dos aeradores.
        ESP_LOGW(
            TAG,
            "DS1307 nao respondeu no endereco 0x%02X - RTC fisico indisponivel nesta placa; "
            "hora e automacao por horario dependerao apenas do NTP",
            DS1307_ADDR
        );
        s_rtc_disponivel = false;
        return ESP_OK;
    }

    s_rtc_disponivel = true;

    ESP_LOGI(
        TAG,
        "DS1307 encontrado no endereco 0x%02X",
        DS1307_ADDR
    );

    ret = timer_carregar_nvs();

    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "Timers iniciados sem configuracao persistida");
    }

    rtc_ds1307_t rtc;
    ret = RTC_DS1307_LerHorario(&rtc);

    if (ret == ESP_OK)
    {
        ret = rtc_aplicar_no_relogio_sistema(&rtc);

        if (ret == ESP_OK)
        {
            ESP_LOGI(TAG, "Hora do DS1307 aplicada ao ESP32");
        }
        else
        {
            ESP_LOGW(TAG, "Hora do DS1307 invalida; aguardando NTP");
        }
    }
    else
    {
        ESP_LOGW(TAG, "RTC encontrado, mas horario ainda nao eh valido");
    }

    return ESP_OK;
}

bool RTC_DS1307_EstaDisponivel(void)
{
    return s_rtc_disponivel;
}

esp_err_t RTC_DS1307_LerHorario(rtc_ds1307_t *rtc)
{
    if (rtc == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t reg = 0x00;
    uint8_t buffer[7];

    esp_err_t ret = i2c_master_write_read_device(
        I2C_PORT,
        DS1307_ADDR,
        &reg,
        1,
        buffer,
        sizeof(buffer),
        pdMS_TO_TICKS(100)
    );

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Erro ao ler horario do DS1307");
        return ret;
    }

    if ((buffer[0] & 0x80U) != 0U)
    {
        ESP_LOGW(TAG, "Oscilador do DS1307 esta parado (bit CH)");
        return ESP_ERR_INVALID_STATE;
    }

    rtc->segundo = bcd_para_dec(buffer[0] & 0x7FU);
    rtc->minuto = bcd_para_dec(buffer[1] & 0x7FU);

    if ((buffer[2] & 0x40U) != 0U)
    {
        uint8_t hora_12 = bcd_para_dec(buffer[2] & 0x1FU);
        bool periodo_pm = (buffer[2] & 0x20U) != 0U;

        if (hora_12 == 12U)
        {
            hora_12 = 0U;
        }

        rtc->hora = (uint8_t)(hora_12 + (periodo_pm ? 12U : 0U));
    }
    else
    {
        rtc->hora = bcd_para_dec(buffer[2] & 0x3FU);
    }

    rtc->dia_semana = bcd_para_dec(buffer[3] & 0x07U);
    rtc->dia = bcd_para_dec(buffer[4] & 0x3FU);
    rtc->mes = bcd_para_dec(buffer[5] & 0x1FU);
    rtc->ano = (uint16_t)(2000U + bcd_para_dec(buffer[6]));

    if (!rtc_horario_valido(rtc))
    {
        ESP_LOGW(TAG, "Horario invalido lido do DS1307");
        return ESP_ERR_INVALID_RESPONSE;
    }

    return ESP_OK;
}

esp_err_t RTC_DS1307_GravarHorario(const rtc_ds1307_t *rtc)
{
    if (!rtc_horario_valido(rtc))
    {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t buffer[8];

    buffer[0] = 0x00;
    buffer[1] = dec_para_bcd(rtc->segundo) & 0x7FU;
    buffer[2] = dec_para_bcd(rtc->minuto);
    buffer[3] = dec_para_bcd(rtc->hora);
    buffer[4] = dec_para_bcd(rtc->dia_semana);
    buffer[5] = dec_para_bcd(rtc->dia);
    buffer[6] = dec_para_bcd(rtc->mes);
    buffer[7] = dec_para_bcd((uint8_t)(rtc->ano - 2000U));

    esp_err_t ret = i2c_master_write_to_device(
        I2C_PORT,
        DS1307_ADDR,
        buffer,
        sizeof(buffer),
        pdMS_TO_TICKS(100)
    );

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Erro ao gravar horario no DS1307");
        return ret;
    }

    ESP_LOGI(TAG, "Horario gravado no DS1307");
    return ESP_OK;
}

// Editado por Eraldo Bispo e Daniel Montanher - integracao - RTC_DS1307_AtualizarHorarioInternet(),
// rtc_ntp_task() e RTC_DS1307_SolicitarSincronizacaoInternet() (versao original de Daniel) foram
// substituidos por este callback unico. Motivo: aquelas funcoes mantinham um SEGUNDO cliente SNTP
// (API legada esp_sntp.h) rodando em paralelo ao cliente SNTP de ntp_kincony.c (API esp_netif_sntp),
// ambos controlando o mesmo modulo SNTP global do lwIP - risco real de reinicializacao concorrente
// do mesmo recurso (SNTP_EVENT/estado interno), alem de duas tasks concorrendo pelo mesmo objetivo.
// Agora existe um unico cliente SNTP (ntp_kincony.c); este modulo apenas grava no DS1307 a hora
// que o NTP acabou de confirmar, via o "sync_cb" configurado em Ntp_Kincony_Iniciar().
//
// Prioridade das fontes de hora, do sistema como um todo:
//   1) NTP valido (assim que sincroniza, corrige o relogio do sistema E o DS1307)
//   2) RTC fisico valido (aplicado ao relogio do sistema no boot, por RTC_DS1307_Iniciar(),
//      antes mesmo do WiFi conectar - mantem a hora correta sem internet, com a bateria do DS1307)
//   3) Hora invalida (nem RTC nem NTP disponiveis) - ver "clock_source":"invalid" em
//      rtc_adicionar_horario_json() / Ntp_Kincony_ObterDataHoraFormatada() ("Sincronizando...")
void RTC_DS1307_NotificarSincronizacaoNtp(const struct timeval *tv)
{
    if (tv == NULL)
    {
        return;
    }

    s_ntp_sincronizado = true;

    if (!s_rtc_disponivel)
    {
        // Sem chip fisico nesta placa: o relogio do sistema (corrigido pelo NTP) e a unica
        // fonte de hora: nao ha onde persistir para sobreviver a queda de energia/reboot.
        return;
    }

    struct tm timeinfo;
    time_t segundos = tv->tv_sec;
    localtime_r(&segundos, &timeinfo);

    rtc_ds1307_t rtc = {
        .segundo = (uint8_t)timeinfo.tm_sec,
        .minuto = (uint8_t)timeinfo.tm_min,
        .hora = (uint8_t)timeinfo.tm_hour,
        .dia_semana = (uint8_t)(timeinfo.tm_wday + 1),
        .dia = (uint8_t)timeinfo.tm_mday,
        .mes = (uint8_t)(timeinfo.tm_mon + 1),
        .ano = (uint16_t)(timeinfo.tm_year + 1900)
    };

    esp_err_t ret = RTC_DS1307_GravarHorario(&rtc);

    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "NTP sincronizado - DS1307 atualizado");
    }
    else
    {
        ESP_LOGW(TAG, "NTP sincronizado, mas falhou ao gravar no DS1307: %s", esp_err_to_name(ret));
    }
}

void RTC_DS1307_Processar(void)
{
    if (s_mutex == NULL || !s_rtc_disponivel)
    {
        return;
    }

    TickType_t agora_tick = xTaskGetTickCount();

    if ((agora_tick - s_ultimo_processamento) <
        pdMS_TO_TICKS(RTC_PROCESSAMENTO_INTERVALO_MS))
    {
        return;
    }

    s_ultimo_processamento = agora_tick;

    rtc_ds1307_t rtc;

    if (RTC_DS1307_LerHorario(&rtc) != ESP_OK)
    {
        return;
    }

    uint32_t assinatura = rtc_assinatura_minuto(&rtc);

    if (assinatura == s_ultima_assinatura_minuto)
    {
        return;
    }

    s_ultima_assinatura_minuto = assinatura;

    rtc_timer_config_t timers_locais[RTC_TIMER_QUANTIDADE];

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        return;
    }

    memcpy(timers_locais, s_timers, sizeof(timers_locais));
    xSemaphoreGive(s_mutex);

    for (uint8_t i = 0U; i < RTC_TIMER_QUANTIDADE; i++)
    {
        const rtc_timer_config_t *timer = &timers_locais[i];

        if (!timer->configurado || !timer->habilitado)
        {
            continue;
        }

        if (rtc.hora == timer->hora_liga &&
            rtc.minuto == timer->minuto_liga)
        {
            timer_executar(i, timer, true);
        }
        else if (rtc.hora == timer->hora_desliga &&
                 rtc.minuto == timer->minuto_desliga)
        {
            timer_executar(i, timer, false);
        }
    }
}

esp_err_t RTC_DS1307_ProcessarComandoMQTT(
    const char *payload,
    char *resposta,
    size_t tamanho_resposta
)
{
    if (payload == NULL || resposta == NULL || tamanho_resposta == 0U)
    {
        return ESP_ERR_INVALID_ARG;
    }

    resposta[0] = '\0';

    cJSON *root = cJSON_Parse(payload);

    if (root == NULL)
    {
        return ESP_ERR_NOT_SUPPORTED;
    }

    cJSON *action = cJSON_GetObjectItemCaseSensitive(root, "action");

    if (!cJSON_IsString(action) || action->valuestring == NULL)
    {
        cJSON_Delete(root);
        return ESP_ERR_NOT_SUPPORTED;
    }

    /*
     * Copia a acao para um buffer local ANTES de qualquer cJSON_Delete(root) que aconteca
     * mais abaixo nesta funcao. action->valuestring pertence ao heap alocado por cJSON_Parse();
     * ele e liberado por cJSON_Delete(root), e usa-lo depois disso (como em alguns pontos desta
     * funcao que respondem com o proprio nome da acao) e use-after-free - a causa raiz da
     * corrupcao do campo "action" observada nas respostas de timer_enable/timer_disable. Com a
     * copia, `acao` nunca mais depende do tempo de vida de `root`.
     */
    char acao_buf[24];
    strncpy(acao_buf, action->valuestring, sizeof(acao_buf) - 1);
    acao_buf[sizeof(acao_buf) - 1] = '\0';
    const char *acao = acao_buf;

    bool comando_suportado =
        strcmp(acao, "timer_set") == 0 ||
        strcmp(acao, "timer_enable") == 0 ||
        strcmp(acao, "timer_disable") == 0 ||
        strcmp(acao, "timer_clear") == 0 ||
        strcmp(acao, "timer_clear_all") == 0 ||
        strcmp(acao, "timer_get") == 0 ||
        strcmp(acao, "rtc_get") == 0 ||
        strcmp(acao, "rtc_sync") == 0;

    if (!comando_suportado)
    {
        cJSON_Delete(root);
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (strcmp(acao, "rtc_sync") == 0)
    {
        // Editado por Eraldo Bispo e Daniel Montanher - integracao - forca uma nova rodada de
        // sincronizacao no cliente SNTP unico (ntp_kincony.c) em vez de abrir um segundo cliente
        // SNTP proprio deste modulo. O resultado chega de forma assincrona em
        // RTC_DS1307_NotificarSincronizacaoNtp() (ver comentario acima desta funcao).
        esp_err_t ret_resync = Ntp_Kincony_ForcarResincronizacao();

        if (ret_resync != ESP_OK)
        {
            ESP_LOGW(TAG, "Falha ao solicitar resincronizacao NTP: %s", esp_err_to_name(ret_resync));
        }

        cJSON_Delete(root);

        return json_resposta_simples(
            "rtc_sync",
            "started",
            0,
            resposta,
            tamanho_resposta
        );
    }

    if (strcmp(acao, "rtc_get") == 0)
    {
        cJSON *saida = cJSON_CreateObject();

        if (saida == NULL)
        {
            cJSON_Delete(root);
            return ESP_ERR_NO_MEM;
        }

        cJSON_AddStringToObject(saida, "action", "rtc_get");
        cJSON_AddStringToObject(saida, "result", "ok");
        rtc_adicionar_horario_json(saida);

        cJSON_Delete(root);
        return json_finalizar(saida, resposta, tamanho_resposta);
    }

    if (strcmp(acao, "timer_get") == 0)
    {
        rtc_timer_config_t copia[RTC_TIMER_QUANTIDADE];

        if (s_mutex == NULL ||
            xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE)
        {
            cJSON_Delete(root);
            return ESP_ERR_INVALID_STATE;
        }

        memcpy(copia, s_timers, sizeof(copia));
        xSemaphoreGive(s_mutex);

        cJSON *saida = cJSON_CreateObject();
        cJSON *array = cJSON_CreateArray();

        if (saida == NULL || array == NULL)
        {
            cJSON_Delete(saida);
            cJSON_Delete(array);
            cJSON_Delete(root);
            return ESP_ERR_NO_MEM;
        }

        cJSON_AddStringToObject(saida, "action", "timer_get");
        cJSON_AddStringToObject(saida, "result", "ok");

        for (uint8_t i = 0U; i < RTC_TIMER_QUANTIDADE; i++)
        {
            cJSON *item = cJSON_CreateObject();

            if (item == NULL)
            {
                cJSON_Delete(saida);
                cJSON_Delete(array);
                cJSON_Delete(root);
                return ESP_ERR_NO_MEM;
            }

            char horario_liga[6];
            char horario_desliga[6];

            formatar_hhmm(
                horario_liga,
                copia[i].hora_liga,
                copia[i].minuto_liga
            );

            formatar_hhmm(
                horario_desliga,
                copia[i].hora_desliga,
                copia[i].minuto_desliga
            );

            cJSON_AddNumberToObject(item, "id", i + 1U);
            cJSON_AddBoolToObject(item, "configured", copia[i].configurado != 0U);
            cJSON_AddBoolToObject(item, "enabled", copia[i].habilitado != 0U);
            cJSON_AddNumberToObject(item, "group", copia[i].grupo);
            cJSON_AddStringToObject(item, "on", horario_liga);
            cJSON_AddStringToObject(item, "off", horario_desliga);

            cJSON_AddItemToArray(array, item);
        }

        cJSON_AddItemToObject(saida, "timers", array);
        rtc_adicionar_horario_json(saida);

        cJSON_Delete(root);
        return json_finalizar(saida, resposta, tamanho_resposta);
    }

    if (strcmp(acao, "timer_clear_all") == 0)
    {
        if (s_mutex == NULL ||
            xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE)
        {
            cJSON_Delete(root);
            return ESP_ERR_INVALID_STATE;
        }

        memset(s_timers, 0, sizeof(s_timers));
        esp_err_t ret = timer_salvar_nvs();

        xSemaphoreGive(s_mutex);
        cJSON_Delete(root);

        json_resposta_simples(
            "timer_clear_all",
            ret == ESP_OK ? "ok" : "error",
            0,
            resposta,
            tamanho_resposta
        );

        return ret;
    }

    cJSON *id_item = cJSON_GetObjectItemCaseSensitive(root, "id");

    if (!cJSON_IsNumber(id_item) ||
        id_item->valueint < 1 ||
        id_item->valueint > RTC_TIMER_QUANTIDADE)
    {
        cJSON_Delete(root);

        json_resposta_simples(
            acao,
            "invalid_id",
            0,
            resposta,
            tamanho_resposta
        );

        return ESP_ERR_INVALID_ARG;
    }

    uint8_t indice = (uint8_t)(id_item->valueint - 1);

    if (strcmp(acao, "timer_clear") == 0)
    {
        if (s_mutex == NULL ||
            xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE)
        {
            cJSON_Delete(root);
            return ESP_ERR_INVALID_STATE;
        }

        memset(&s_timers[indice], 0, sizeof(s_timers[indice]));
        esp_err_t ret = timer_salvar_nvs();

        xSemaphoreGive(s_mutex);
        cJSON_Delete(root);

        json_resposta_simples(
            "timer_clear",
            ret == ESP_OK ? "ok" : "error",
            indice + 1,
            resposta,
            tamanho_resposta
        );

        return ret;
    }

    if (strcmp(acao, "timer_enable") == 0 ||
        strcmp(acao, "timer_disable") == 0)
    {
        if (s_mutex == NULL ||
            xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE)
        {
            cJSON_Delete(root);
            return ESP_ERR_INVALID_STATE;
        }

        if (!s_timers[indice].configurado &&
            strcmp(acao, "timer_enable") == 0)
        {
            xSemaphoreGive(s_mutex);
            cJSON_Delete(root);

            json_resposta_simples(
                acao,
                "not_configured",
                indice + 1,
                resposta,
                tamanho_resposta
            );

            return ESP_ERR_INVALID_STATE;
        }

        s_timers[indice].habilitado =
            (strcmp(acao, "timer_enable") == 0) ? 1U : 0U;

        esp_err_t ret = timer_salvar_nvs();

        xSemaphoreGive(s_mutex);
        cJSON_Delete(root);

        json_resposta_simples(
            acao,
            ret == ESP_OK ? "ok" : "error",
            indice + 1,
            resposta,
            tamanho_resposta
        );

        return ret;
    }

    cJSON *enabled_item =
        cJSON_GetObjectItemCaseSensitive(root, "enabled");
    cJSON *group_item =
        cJSON_GetObjectItemCaseSensitive(root, "group");
    cJSON *on_item =
        cJSON_GetObjectItemCaseSensitive(root, "on");
    cJSON *off_item =
        cJSON_GetObjectItemCaseSensitive(root, "off");

    bool habilitado = false;
    uint8_t hora_liga = 0;
    uint8_t minuto_liga = 0;
    uint8_t hora_desliga = 0;
    uint8_t minuto_desliga = 0;

    bool campos_validos =
        json_obter_enabled(enabled_item, &habilitado) &&
        cJSON_IsNumber(group_item) &&
        group_item->valueint >= 0 &&
        group_item->valueint <= LOGICA_CONTROLE_NUM_GRUPOS &&
        cJSON_IsString(on_item) &&
        cJSON_IsString(off_item) &&
        horario_texto_valido(
            on_item->valuestring,
            &hora_liga,
            &minuto_liga
        ) &&
        horario_texto_valido(
            off_item->valuestring,
            &hora_desliga,
            &minuto_desliga
        );

    if (!campos_validos ||
        (hora_liga == hora_desliga &&
         minuto_liga == minuto_desliga))
    {
        cJSON_Delete(root);

        json_resposta_simples(
            "timer_set",
            "invalid_fields",
            indice + 1,
            resposta,
            tamanho_resposta
        );

        return ESP_ERR_INVALID_ARG;
    }

    rtc_timer_config_t novo = {
        .configurado = 1U,
        .habilitado = habilitado ? 1U : 0U,
        .grupo = (uint8_t)group_item->valueint,
        .hora_liga = hora_liga,
        .minuto_liga = minuto_liga,
        .hora_desliga = hora_desliga,
        .minuto_desliga = minuto_desliga,
        .reservado = 0U
    };

    if (s_mutex == NULL ||
        xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_STATE;
    }

    s_timers[indice] = novo;
    esp_err_t ret = timer_salvar_nvs();

    xSemaphoreGive(s_mutex);
    cJSON_Delete(root);

    json_resposta_simples(
        "timer_set",
        ret == ESP_OK ? "ok" : "error",
        indice + 1,
        resposta,
        tamanho_resposta
    );

    return ret;
}
