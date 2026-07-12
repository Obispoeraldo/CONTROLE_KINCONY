/*
 * ntp_kincony.c
 *
 * Criado por Eraldo Bispo - 24/06/2026
 * Ver motivo e contrato completo em ntp_kincony.h
 *
 * Editado por Eraldo Bispo e Daniel Montanher - integracao - registra
 * ntp_sync_callback() como "sync_cb" do cliente SNTP para acionar
 * RTC_DS1307_NotificarSincronizacaoNtp() (grava a hora corrigida no DS1307,
 * quando presente). Este e o UNICO ponto do projeto que chama
 * esp_netif_sntp_init()/esp_netif_sntp_start() - ver aviso em ntp_kincony.h.
 */

#include "ntp_kincony.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "esp_netif_sntp.h"

#include "rtc_ds1307.h"

static const char *TAG = "NTP_KINCONY";
static volatile bool s_iniciado = false;

static void ntp_sync_callback(struct timeval *tv)
{
    ESP_LOGI(TAG, "NTP sincronizado");
    RTC_DS1307_NotificarSincronizacaoNtp(tv);
}

esp_err_t Ntp_Kincony_Iniciar(const char *servidor, int8_t fuso_horas)
{
    char tz[16];
    snprintf(tz, sizeof(tz), "<%+03d>%d", fuso_horas, -fuso_horas);
    setenv("TZ", tz, 1);
    tzset();

    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(servidor);
    config.wait_for_sync = false;
    config.sync_cb = ntp_sync_callback;

    esp_err_t ret = esp_netif_sntp_init(&config);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Erro ao iniciar SNTP (servidor '%s'): 0x%x", servidor, ret);
        return ret;
    }

    s_iniciado = true;

    ESP_LOGI(TAG, "SNTP iniciado - servidor '%s', fuso %s%d", servidor, fuso_horas >= 0 ? "+" : "", fuso_horas);

    return ESP_OK;
}

esp_err_t Ntp_Kincony_ForcarResincronizacao(void)
{
    if (!s_iniciado)
    {
        return ESP_ERR_INVALID_STATE;
    }

    return esp_netif_sntp_start();
}

void Ntp_Kincony_ObterDataHoraFormatada(char *destino, size_t tamanho)
{
    if (destino == NULL || tamanho == 0)
    {
        return;
    }

    time_t agora = time(NULL);
    struct tm horario_local;
    localtime_r(&agora, &horario_local);

    // Editado por Eraldo Bispo - 24/06/2026 - sem NTP sincronizado, o relogio do ESP32 comeca
    // do epoch (1970) - usamos o ano como sinal simples de "ainda nao sincronizou".
    if (horario_local.tm_year + 1900 < 2024)
    {
        strncpy(destino, "Sincronizando...", tamanho - 1);
        destino[tamanho - 1] = '\0';
        return;
    }

    strftime(destino, tamanho, "%d/%m/%Y %H:%M:%S", &horario_local);
}
