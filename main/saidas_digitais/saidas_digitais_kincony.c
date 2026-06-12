#include "saidas_digitais_kincony.h"

#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "SAIDAS"

#define I2C_PORT        I2C_NUM_0
#define TEMPO_I2C_MS    100

static uint8_t saidas_estado = 0x00;
static uint8_t saidas_online = 0;

static esp_err_t Saidas_Kincony_Enviar(void)
{
    /*
        No firmware:
        bit 1 = saída ligada
        bit 0 = saída desligada

        No PCF8574/relé acionado por negativo:
        bit 0 = relé ligado
        bit 1 = relé desligado

        Por isso envia invertido.
    */
    uint8_t valor_pcf = ~saidas_estado;

    esp_err_t ret = i2c_master_write_to_device(
        I2C_PORT,
        SAIDAS_PCF_ADDR,
        &valor_pcf,
        1,
        pdMS_TO_TICKS(TEMPO_I2C_MS)
    );

    if (ret != ESP_OK)
    {
        saidas_online = 0;
        ESP_LOGW(TAG, "Falha ao escrever PCF8574 das saidas");
        return ret;
    }

    saidas_online = 1;
    return ESP_OK;
}

esp_err_t Saidas_Kincony_Iniciar(void)
{
    saidas_estado = 0x00;

    esp_err_t ret = Saidas_Kincony_Enviar();

    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "Saidas iniciadas no PCF8574 endereco 0x%02X", SAIDAS_PCF_ADDR);
    }

    return ret;
}

esp_err_t Saidas_Kincony_Ligar(saida_kincony_t saida)
{
    if (saida >= SAIDAS_KINCONY_QTD)
    {
        return ESP_ERR_INVALID_ARG;
    }

    saidas_estado |= (1 << saida);

    return Saidas_Kincony_Enviar();
}

esp_err_t Saidas_Kincony_Desligar(saida_kincony_t saida)
{
    if (saida >= SAIDAS_KINCONY_QTD)
    {
        return ESP_ERR_INVALID_ARG;
    }

    saidas_estado &= ~(1 << saida);

    return Saidas_Kincony_Enviar();
}

esp_err_t Saidas_Kincony_DesligarTodas(void)
{
    saidas_estado = 0x00;

    return Saidas_Kincony_Enviar();
}

bool Saidas_Kincony_Get(saida_kincony_t saida)
{
    if (saida >= SAIDAS_KINCONY_QTD)
    {
        return false;
    }

    return (saidas_estado & (1 << saida)) ? true : false;
}

uint8_t Saidas_Kincony_GetEstado(void)
{
    return saidas_estado;
}

bool Saidas_Kincony_IsOnline(void)
{
    return saidas_online;
}