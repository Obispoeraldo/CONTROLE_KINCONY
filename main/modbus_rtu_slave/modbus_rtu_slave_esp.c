#include "esp_err.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "modbus_rtu_slave_esp.h"
#include "esp_modbus_slave.h"

static const char *TAG = "MB_RTU_SLAVE";

static void *mb_handle = NULL;
 
uint16_t holding_registers[MB_HOLDING_REG_QTD] = {0};

esp_err_t ModbusRTU_Slave_Init(void)
{
    ESP_LOGI(TAG, "Iniciando Modbus RTU Slave...");

    mb_communication_info_t comm_config = {
        .ser_opts.port = MB_RTU_UART_PORT,
        .ser_opts.mode = MB_RTU,
        .ser_opts.baudrate = MB_RTU_BAUDRATE,
        .ser_opts.parity = MB_PARITY_NONE,
        .ser_opts.uid = MB_RTU_SLAVE_ID,
        .ser_opts.data_bits = UART_DATA_8_BITS,
        .ser_opts.stop_bits = UART_STOP_BITS_1
    };

    esp_err_t err;

    err = mbc_slave_create_serial(&comm_config, &mb_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Erro mbc_slave_create_serial: 0x%x", err);
        return err;
    }

    // Editado por Eraldo Bispo - 21/06/2026 16:08 - removido o pino RTS/DE-RE e o modo
    // UART_MODE_RS485_HALF_DUPLEX. Na KC868-A6 v1.3 o transceptor RS485 e o MAX13487EESA, que
    // tem controle de direcao automatico (sente a propria linha TX), sem pino de habilitacao
    // controlado pelo ESP32 - por isso a placa nao roteia nenhum GPIO para DE/RE. Usar
    // UART_MODE_RS485_HALF_DUPLEX sem RTS valido nao tinha efeito nenhum na direcao real do
    // barramento; UART normal funciona igual aqui, ja que o chip cuida da troca TX/RX sozinho.
    err = uart_set_pin(
        MB_RTU_UART_PORT,
        MB_RTU_TX_PIN,
        MB_RTU_RX_PIN,
        UART_PIN_NO_CHANGE,
        UART_PIN_NO_CHANGE
    );

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Erro uart_set_pin: 0x%x", err);
        return err;
    }

    err = uart_set_mode(MB_RTU_UART_PORT, UART_MODE_UART);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Erro uart_set_mode: 0x%x", err);
        return err;
    }

    mb_register_area_descriptor_t holding_area = {
        .type = MB_PARAM_HOLDING,
        .start_offset = 0,
        .address = holding_registers,
        .size = sizeof(holding_registers),
        .access = MB_ACCESS_RW
    };

    err = mbc_slave_set_descriptor(mb_handle, holding_area);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Erro mbc_slave_set_descriptor: 0x%x", err);
        return err;
    }

    err = mbc_slave_start(mb_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Erro mbc_slave_start: 0x%x", err);
        return err;
    }

    ESP_LOGI(TAG, "Modbus RTU Slave iniciado");
    ESP_LOGI(TAG, "ID=%d UART=%d Baud=%d", MB_RTU_SLAVE_ID, MB_RTU_UART_PORT, MB_RTU_BAUDRATE);
    // Editado por Eraldo Bispo - 21/06/2026 16:08 - log sem DE/RE (pino removido, ver comentario
    // em ModbusRTU_Slave_Init() sobre o MAX13487EESA com direcao automatica)
    ESP_LOGI(TAG, "TX=%d RX=%d", MB_RTU_TX_PIN, MB_RTU_RX_PIN);
    ESP_LOGI(TAG, "Holding registers: %d", MB_HOLDING_REG_QTD);

    return ESP_OK;
}