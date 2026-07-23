/*
 * wifi_config_button.c
 *
 * Ver wifi_config_button.h para a descricao dos 3 niveis de pressao.
 */

#include "wifi_config_button.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"

#include "wifi_kincony.h"
#include "config_server_kincony.h"

#define TAG "WIFI_BOTAO_CFG"

// GPIO0 = botao "BOOT" nativo do ESP32 (nivel baixo quando pressionado). Livre neste
// projeto - I2C (entradas/saidas/RTC) usa GPIO4/GPIO15, ver entradas_kincony.c.
#define WIFI_CONFIG_BUTTON_GPIO         GPIO_NUM_0

#define WIFI_CONFIG_BUTTON_MS_PORTAL    5000U
#define WIFI_CONFIG_BUTTON_MS_APAGAR    10000U
#define WIFI_CONFIG_BUTTON_MS_FABRICA   30000U

static bool s_pressionado_anterior = false;
static bool s_disparado_portal  = false;
static bool s_disparado_apagar  = false;
static bool s_disparado_fabrica = false;
static int64_t s_tick_inicio_us = 0;

esp_err_t Wifi_Config_Button_Iniciar(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << WIFI_CONFIG_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    esp_err_t ret = gpio_config(&cfg);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Erro ao configurar botao de configuracao (GPIO%d): %s",
                  WIFI_CONFIG_BUTTON_GPIO, esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Botao de configuracao pronto (GPIO%d / BOOT). Segurar: 5s=portal, "
              "10s=apagar WiFi salvo, 30s=restaurar fabrica.", WIFI_CONFIG_BUTTON_GPIO);

    return ESP_OK;
}

void Wifi_Config_Button_Processar(void)
{
    bool pressionado = (gpio_get_level(WIFI_CONFIG_BUTTON_GPIO) == 0);

    if (pressionado && !s_pressionado_anterior)
    {
        // Borda de descida - inicio de uma nova pressao continua.
        s_tick_inicio_us = esp_timer_get_time();
        s_disparado_portal = false;
        s_disparado_apagar = false;
        s_disparado_fabrica = false;
    }

    if (pressionado)
    {
        uint32_t decorrido_ms = (uint32_t)((esp_timer_get_time() - s_tick_inicio_us) / 1000);

        if (decorrido_ms >= WIFI_CONFIG_BUTTON_MS_FABRICA && !s_disparado_fabrica)
        {
            s_disparado_fabrica = true;

            ESP_LOGW(TAG, "Botao segurado 30s: restaurando configuracao de fabrica e reiniciando...");
            Config_Server_Kincony_RestaurarConfiguracaoFabrica();
            esp_restart();
        }
        else if (decorrido_ms >= WIFI_CONFIG_BUTTON_MS_APAGAR && !s_disparado_apagar)
        {
            s_disparado_apagar = true;

            ESP_LOGW(TAG, "Botao segurado 10s: apagando credenciais WiFi salvas e abrindo portal...");
            Config_Server_Kincony_ApagarCredenciaisWifi();
            Wifi_Kincony_AbrirPortalConfiguracao(true);
        }
        else if (decorrido_ms >= WIFI_CONFIG_BUTTON_MS_PORTAL && !s_disparado_portal)
        {
            s_disparado_portal = true;

            ESP_LOGW(TAG, "Botao segurado 5s: abrindo portal de configuracao (rede atual preservada)...");
            Wifi_Kincony_AbrirPortalConfiguracao(true);
        }
    }

    s_pressionado_anterior = pressionado;
}
