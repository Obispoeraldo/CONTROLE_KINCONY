//exemplo de OTA no .main
/*
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "wifi_app.h"
#include "ota_github.h"

void app_main(void)
{ 
    wifi_app_init();

    if (wifi_app_is_connected())
    {
        ota_github_check_update();
    }

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

*/


#include "ota_github.h"

#include <string.h>
#include <stdlib.h>

#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "wifi_kincony.h"

#define OTA_MANIFEST_URL "https://raw.githubusercontent.com/dnx98/OTA/main/firmware/manifest.json"

#define OTA_HTTP_TIMEOUT_MS 10000
#define OTA_BUFFER_SIZE     2048

static const char *TAG = "OTA_GITHUB";

static char ota_buffer[OTA_BUFFER_SIZE];

uint8_t versao_firmware_atual = 1;

    void verifica_atualizacao(void){

     if(versao_firmware_atual)
        {
            if (Wifi_Kincony_IsConectado())
        {
            ota_github_check_update();
            versao_firmware_atual = 0;
         } else
        {
            versao_firmware_atual = 1;
         }
        }
    }

static int version_compare(const char *v1, const char *v2)
{
    int major1 = 0, minor1 = 0, patch1 = 0;
    int major2 = 0, minor2 = 0, patch2 = 0;

    sscanf(v1, "%d.%d.%d", &major1, &minor1, &patch1);
    sscanf(v2, "%d.%d.%d", &major2, &minor2, &patch2);

    if (major1 != major2) return major1 - major2;
    if (minor1 != minor2) return minor1 - minor2;
    return patch1 - patch2;
}

static esp_err_t ota_download_manifest(char *buffer, int buffer_size)
{
    esp_http_client_config_t config = {
        .url = OTA_MANIFEST_URL,
        .timeout_ms = OTA_HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    if (client == NULL)
    {
        ESP_LOGE(TAG, "Falha ao inicializar HTTP client");
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_open(client, 0);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Falha ao abrir conexão HTTP: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    int content_length = esp_http_client_fetch_headers(client);

    if (content_length >= buffer_size)
    {
        ESP_LOGE(TAG, "Manifest muito grande");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    int total_read = 0;

    while (total_read < buffer_size - 1)
    {
        int read_len = esp_http_client_read(client,
                                            buffer + total_read,
                                            buffer_size - total_read - 1);

        if (read_len <= 0)
        {
            break;
        }

        total_read += read_len;
    }

    buffer[total_read] = '\0';

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (total_read <= 0)
    {
        ESP_LOGE(TAG, "Manifest vazio ou erro de leitura");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Manifest recebido:");
    ESP_LOGI(TAG, "%s", buffer);

    return ESP_OK;
}

static esp_err_t ota_execute_update(const char *firmware_url)
{
    ESP_LOGW(TAG, "Iniciando OTA");
    ESP_LOGW(TAG, "URL: %s", firmware_url);

    esp_http_client_config_t http_config = {
        .url = firmware_url,
        .timeout_ms = OTA_HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .keep_alive_enable = true,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
    };

    esp_err_t ret = esp_https_ota(&ota_config);

    if (ret == ESP_OK)
    {
        ESP_LOGW(TAG, "OTA concluído. Reiniciando...");
        esp_restart();
    }
    else
    {
        ESP_LOGE(TAG, "Falha no OTA: %s", esp_err_to_name(ret));
    }

    return ret;
}

esp_err_t ota_github_check_update(void)
{
    ESP_LOGI(TAG, "Versão atual: %s", FIRMWARE_VERSION);

    esp_err_t err = ota_download_manifest(ota_buffer, sizeof(ota_buffer));

    if (err != ESP_OK)
    {
        return err;
    }

    cJSON *root = cJSON_Parse(ota_buffer);

    if (root == NULL)
    {
        ESP_LOGE(TAG, "Erro ao interpretar manifest.json");
        return ESP_FAIL;
    }

    cJSON *version = cJSON_GetObjectItem(root, "version");
    cJSON *url = cJSON_GetObjectItem(root, "url");

    if (!cJSON_IsString(version) || !cJSON_IsString(url))
    {
        ESP_LOGE(TAG, "manifest.json inválido");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Versão disponível: %s", version->valuestring);

    if (version_compare(version->valuestring, FIRMWARE_VERSION) > 0)
    {
        ESP_LOGW(TAG, "Nova versão encontrada");

        char firmware_url[512];
        strncpy(firmware_url, url->valuestring, sizeof(firmware_url) - 1);
        firmware_url[sizeof(firmware_url) - 1] = '\0';

        cJSON_Delete(root);

        return ota_execute_update(firmware_url);
    }

    ESP_LOGI(TAG, "Firmware já está atualizado");

    cJSON_Delete(root);

    return ESP_OK;
}