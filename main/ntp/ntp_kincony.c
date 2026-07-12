/*
 * ntp_kincony.c
 *
 * Criado por Eraldo Bispo - 24/06/2026
 * Ver motivo e contrato completo em ntp_kincony.h
 */

#include "ntp_kincony.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "esp_netif_sntp.h"

static const char *TAG = "NTP_KINCONY";

esp_err_t Ntp_Kincony_Iniciar(const char *servidor, int8_t fuso_horas)
{
    char tz[16];
    snprintf(tz, sizeof(tz), "<%+03d>%d", fuso_horas, -fuso_horas);
    setenv("TZ", tz, 1);
    tzset();

    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(servidor);
    config.wait_for_sync = false;

    esp_err_t ret = esp_netif_sntp_init(&config);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Erro ao iniciar SNTP (servidor '%s'): 0x%x", servidor, ret);
        return ret;
    }

    ESP_LOGI(TAG, "SNTP iniciado - servidor '%s', fuso %s%d", servidor, fuso_horas >= 0 ? "+" : "", fuso_horas);

    return ESP_OK;
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
