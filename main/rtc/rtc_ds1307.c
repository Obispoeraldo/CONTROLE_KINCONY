#include "rtc_ds1307.h"

#include <stdio.h>
#include <time.h>
#include <sys/time.h>

#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "RTC_DS1307"

#define I2C_PORT        I2C_NUM_0
#define DS1307_ADDR     0x68

static uint8_t dec_para_bcd(uint8_t valor)
{
    return ((valor / 10) << 4) | (valor % 10);
}

static uint8_t bcd_para_dec(uint8_t valor)
{
    return ((valor >> 4) * 10) + (valor & 0x0F);
}

esp_err_t RTC_DS1307_Iniciar(void)
{
    uint8_t reg = 0x00;
    uint8_t valor = 0;

    printf("Scanner I2C finalizado.\n");

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
        ESP_LOGE(TAG, "DS1307 nao respondeu no endereco 0x%02X", DS1307_ADDR);
        return ret;
    }

    ESP_LOGI(TAG, "DS1307 encontrado no endereco 0x%02X", DS1307_ADDR);

    return ESP_OK;
}

esp_err_t RTC_DS1307_LerHorario(rtc_ds1307_t *rtc)
{
    if (rtc == NULL)
        return ESP_ERR_INVALID_ARG;

    uint8_t reg = 0x00;
    uint8_t buffer[7];

    esp_err_t ret = i2c_master_write_read_device(
        I2C_PORT,
        DS1307_ADDR,
        &reg,
        1,
        buffer,
        7,
        pdMS_TO_TICKS(100)
    );

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Erro ao ler horario do DS1307");
        return ret;
    }

    rtc->segundo    = bcd_para_dec(buffer[0] & 0x7F);
    rtc->minuto     = bcd_para_dec(buffer[1] & 0x7F);
    rtc->hora       = bcd_para_dec(buffer[2] & 0x3F);
    rtc->dia_semana = bcd_para_dec(buffer[3] & 0x07);
    rtc->dia        = bcd_para_dec(buffer[4] & 0x3F);
    rtc->mes        = bcd_para_dec(buffer[5] & 0x1F);
    rtc->ano        = 2000 + bcd_para_dec(buffer[6]);

    return ESP_OK;
}

esp_err_t RTC_DS1307_GravarHorario(const rtc_ds1307_t *rtc)
{
    if (rtc == NULL)
        return ESP_ERR_INVALID_ARG;

    uint8_t buffer[8];

    buffer[0] = 0x00;
    buffer[1] = dec_para_bcd(rtc->segundo) & 0x7F;
    buffer[2] = dec_para_bcd(rtc->minuto);
    buffer[3] = dec_para_bcd(rtc->hora);
    buffer[4] = dec_para_bcd(rtc->dia_semana);
    buffer[5] = dec_para_bcd(rtc->dia);
    buffer[6] = dec_para_bcd(rtc->mes);
    buffer[7] = dec_para_bcd(rtc->ano - 2000);

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

esp_err_t RTC_DS1307_AtualizarHorarioInternet(void)
{
    ESP_LOGI(TAG, "Atualizando horario via internet...");

    setenv("TZ", "BRT3", 1);
    tzset();

    esp_sntp_stop();

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");
    esp_sntp_init();

    time_t agora = 0;
    struct tm timeinfo = {0};

    for (int tentativa = 0; tentativa < 20; tentativa++)
    {
        time(&agora);
        localtime_r(&agora, &timeinfo);

        if (timeinfo.tm_year >= (2024 - 1900))
        {
            rtc_ds1307_t rtc = {
                .segundo = timeinfo.tm_sec,
                .minuto = timeinfo.tm_min,
                .hora = timeinfo.tm_hour,
                .dia_semana = timeinfo.tm_wday + 1,
                .dia = timeinfo.tm_mday,
                .mes = timeinfo.tm_mon + 1,
                .ano = timeinfo.tm_year + 1900,
            };

            esp_err_t ret = RTC_DS1307_GravarHorario(&rtc);

            esp_sntp_stop();

            return ret;
        }

        ESP_LOGI(TAG, "Aguardando NTP...");
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    esp_sntp_stop();

    ESP_LOGE(TAG, "Falha ao obter horario via internet");

    return ESP_FAIL;
}