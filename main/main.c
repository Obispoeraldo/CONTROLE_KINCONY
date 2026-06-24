#include "entradas_kincony.h"
#include "saidas_digitais_kincony.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "modbus_rtu_slave_esp.h"
#include "mqtt_kincony.h"
#include "wifi_kincony.h"
#include "ota_github.h"
#include "logica_controle.h"
#include "config_server_kincony.h"
#include "rtc_ds1307.h"

void app_main(void)
{
    // Inicialização dos componentes
    ESP_ERROR_CHECK(Entradas_Kincony_Iniciar());
    ESP_ERROR_CHECK(Saidas_Kincony_Iniciar());

    Logica_Controle_Iniciar();

    

     RTC_DS1307_Iniciar();

    // Criado por Eraldo Bispo — carrega WiFi/broker da NVS (ou do menuconfig na primeira vez)
    ESP_ERROR_CHECK(Config_Server_Kincony_Iniciar());

    char wifi_ssid[32];
    char wifi_senha[64];
    char broker_uri[128];

    Config_Server_Kincony_GetWifiSsid(wifi_ssid, sizeof(wifi_ssid));
    Config_Server_Kincony_GetWifiSenha(wifi_senha, sizeof(wifi_senha));
    Config_Server_Kincony_GetBrokerUri(broker_uri, sizeof(broker_uri));

    //inicializa o wifi antes do mqtt para garantir que a conexão esteja pronta
    ESP_ERROR_CHECK(Wifi_Kincony_Init(wifi_ssid, wifi_senha));

      rtc_ds1307_t horario;

    // Criado por Eraldo Bispo — se a senha WiFi configurada pelo painel estiver errada, o ESP nao consegue
    // mais ser acessado pela rede. Aqui aguardamos o resultado da conexao e, se falhar, revertemos
    // automaticamente para o ultimo WiFi que funcionou (salvo como backup ao editar pelo painel).
    if (!Wifi_Kincony_EsperarResultado(30000))
    {
        if (Config_Server_Kincony_RestaurarBackupWifi() == ESP_OK)
        {
            esp_restart();
        }
    }

    // Criado por Eraldo Bispo — painel web de configuracao, acessivel pelo IP do ESP (ver no monitor)
    // apos conectar. Login padrao: usuario "aquapulse", senha "aquapulse2026" (alteravel no menuconfig)
    ESP_ERROR_CHECK(Config_Server_Kincony_IniciarHttp());

    //inicia mqtt
    Mqtt_Kincony_Init(broker_uri);

    while (1)
    {
    // Verificar se há atualização OTA
    verifica_atualizacao();
    // Processar entradas digitais
    Entradas_Kincony_Processar();
    // Processar lógica de controle do sistema
    Logica_Controle_Processar();
    // Processar MQTT (publicação de monitoramento e recebimento de comandos)
    Mqtt_Kincony_Processar();     
    
 if (RTC_DS1307_LerHorario(&horario) == ESP_OK)
        {
            printf("RTC: %02d/%02d/%04d %02d:%02d:%02d\n",
                   horario.dia,
                   horario.mes,
                   horario.ano,
                   horario.hora,
                   horario.minuto,
                   horario.segundo);
        }


    vTaskDelay(pdMS_TO_TICKS(5000));
     }   
    }
