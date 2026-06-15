#include "entradas_kincony.h"
#include "saidas_digitais_kincony.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "modbus_rtu_slave_esp.h"
#include "mqtt_kincony.h"
#include "wifi_kincony.h"
#include "ota_github.h"
#include "logica_controle.h"

void app_main(void)
{ 
    // Inicialização dos componentes
    ESP_ERROR_CHECK(Entradas_Kincony_Iniciar());
    ESP_ERROR_CHECK(Saidas_Kincony_Iniciar());
    
    Logica_Controle_Iniciar();
    //inicializa o wifi antes do mqtt para garantir que a conexão esteja pronta
    ESP_ERROR_CHECK(Wifi_Kincony_Init("Montanher", "montanher123"));
    //inicia mqtt
    Mqtt_Kincony_Init(MQTT_KINCONY_BROKER_URI);

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

    vTaskDelay(pdMS_TO_TICKS(5000));
     }   
    }
