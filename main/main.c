#include "entradas_kincony.h"

static i2c_master_bus_handle_t i2c_bus = NULL;

void app_main(void)
{
    i2c_master_bus_config_t bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = 22,
        .sda_io_num = 21,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &i2c_bus));

    ESP_ERROR_CHECK(Entradas_Kincony_Init(i2c_bus));

    while (1)
    {
        Entradas_Kincony_Atualizar();

        if (Entradas_Kincony_Get(ENTRADA_1))
        {
            printf("Entrada 1 acionada\n");
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}