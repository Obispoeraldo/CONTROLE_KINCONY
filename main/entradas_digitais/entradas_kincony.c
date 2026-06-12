#include "entradas_kincony.h"

#include <stdio.h>
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "ENTRADAS"

#define I2C_PORT        I2C_NUM_0
#define I2C_SDA         4
#define I2C_SCL         15
#define PCF8574_ADDR    0x22

#define I2C_FREQ_HZ     50000
#define TEMPO_LEITURA_MS 100

uint8_t entrada_1 = 0;
uint8_t entrada_2 = 0;
uint8_t entrada_3 = 0;
uint8_t entrada_4 = 0;
uint8_t entrada_5 = 0;
uint8_t entrada_6 = 0;
uint8_t entrada_7 = 0;
uint8_t entrada_8 = 0;

uint8_t grupo_motor1 = 0;
uint8_t grupo_motor2 = 0;
uint8_t grupo_motor3 = 0;
uint8_t grupo_motor4 = 0;
uint8_t grupo_motor5 = 0;
uint8_t grupo_motor6 = 0;
uint8_t chave_remoto = 0;

static uint8_t entradas_estado = 0;
static uint8_t entradas_online = 0;
static TickType_t ultima_leitura = 0;

esp_err_t Entradas_Kincony_Iniciar(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_SDA,
        .scl_io_num = I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_FREQ_HZ,
    };

    esp_err_t ret;

    ret = i2c_param_config(I2C_PORT, &conf);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Erro i2c_param_config");
        return ret;
    }

    ret = i2c_driver_install(I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Erro i2c_driver_install");
        return ret;
    }

    ESP_LOGI(TAG, "I2C iniciado SDA=%d SCL=%d ADDR=0x%02X", I2C_SDA, I2C_SCL, PCF8574_ADDR);

    return ESP_OK;
}

static esp_err_t Entradas_Kincony_Ler(void)
{
    uint8_t valor_lido = 0xFF;

    esp_err_t ret = i2c_master_read_from_device(
        I2C_PORT,
        PCF8574_ADDR,
        &valor_lido,
        1,
        pdMS_TO_TICKS(100)
    );

    if (ret != ESP_OK)
    {
        entradas_online = 0;
        ESP_LOGW(TAG, "Falha ao ler PCF8574");
        return ret;
    }

    entradas_estado = ~valor_lido;

    entrada_1 = (entradas_estado & (1 << 0)) ? 1 : 0;
    entrada_2 = (entradas_estado & (1 << 1)) ? 1 : 0;
    entrada_3 = (entradas_estado & (1 << 2)) ? 1 : 0;
    entrada_4 = (entradas_estado & (1 << 3)) ? 1 : 0;
    entrada_5 = (entradas_estado & (1 << 4)) ? 1 : 0;
    entrada_6 = (entradas_estado & (1 << 5)) ? 1 : 0;
    entrada_7 = (entradas_estado & (1 << 6)) ? 1 : 0;
    entrada_8 = (entradas_estado & (1 << 7)) ? 1 : 0;

    grupo_motor1 = entrada_1;
    grupo_motor2 = entrada_2;
    grupo_motor3 = entrada_3;
    grupo_motor4 = entrada_4;
    grupo_motor5 = entrada_5;
    grupo_motor6 = entrada_6;
    chave_remoto = entrada_7;

    entradas_online = 1;

    return ESP_OK;
}

void Entradas_Kincony_Processar(void)
{
    TickType_t agora = xTaskGetTickCount();

    if ((agora - ultima_leitura) >= pdMS_TO_TICKS(TEMPO_LEITURA_MS))
    {
        ultima_leitura = agora;
        Entradas_Kincony_Ler();
    }
}

uint8_t Entradas_Kincony_Get(entrada_kincony_t entrada)
{
    return (entradas_estado & (1 << entrada)) ? 1 : 0;
}

uint8_t Entradas_Kincony_GetEstado(void)
{
    return entradas_estado;
}

bool Entradas_Kincony_IsOnline(void)
{
    return entradas_online;
}