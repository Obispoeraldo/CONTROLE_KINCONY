#include "entradas_kincony.h"
#include "saidas_digitais_kincony.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "modbus_rtu_slave_esp.h"
#include "mqtt_kincony.h"
#include "wifi_kincony.h"
#include "ota_github.h"

void app_main(void)
{ 

    ESP_ERROR_CHECK(Entradas_Kincony_Iniciar());
        ESP_ERROR_CHECK(Saidas_Kincony_Iniciar());

       
    //ESP_ERROR_CHECK(Wifi_Kincony_Init("iPhone de Daniel", "12345679"));

        uint8_t umavez = 1;

    while (1)
    {

      /*  if(umavez)
        {
            if (Wifi_Kincony_IsConectado())
        {
            ota_github_check_update();
            umavez = 0;
         } else
        {
            umavez = 1;
         }
        }*/
        
  Entradas_Kincony_Processar();

        printf("E1=%d E2=%d E3=%d E4=%d E5=%d E6=%d E7=%d E8=%d\n",
               entrada_1, entrada_2, entrada_3, entrada_4,
               entrada_5, entrada_6, entrada_7, entrada_8);

                if (grupo_motor1)
        {
            Saidas_Kincony_Ligar(SAIDA_1);
        }
        else
        {
            Saidas_Kincony_Desligar(SAIDA_1);
        }
               

        vTaskDelay(pdMS_TO_TICKS(500));


    }

        
    }
