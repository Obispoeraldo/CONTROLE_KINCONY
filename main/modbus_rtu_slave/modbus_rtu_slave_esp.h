#ifndef MODBUS_RTU_SLAVE_ESP_H
#define MODBUS_RTU_SLAVE_ESP_H

#include <stdint.h>
#include "esp_err.h"
#include "driver/uart.h"

// ===============================
// CONFIGURAÇÃO DO MODBUS
// ===============================
 
#define MB_RTU_SLAVE_ID        1
#define MB_RTU_UART_PORT       UART_NUM_2
// Editado por Eraldo Bispo - 21/06/2026 16:08 - pinos TX/RX corrigidos para os pinos reais do
// RS485 da KC868-A6 v1.3 (GPIO27=TX, GPIO14=RX, confirmado no datasheet/comunidade ESPHome e
// Tasmota). Os antigos (17/16) sao pinos de uso geral da placa, nao chegam ao transceptor RS485
// fisico - por isso o Modbus RTU nunca se comunicava com um master externo no conector RS485.
#define MB_RTU_TX_PIN          27
#define MB_RTU_RX_PIN          14
#define MB_RTU_BAUDRATE        9600

// Altere só isso para mudar a quantidade de holding registers
#define MB_HOLDING_REG_QTD     20

// ===============================
// MAPA HOLDING REGISTER
// ===============================

extern uint16_t holding_registers[MB_HOLDING_REG_QTD];

// ===============================
// FUNÇÕES
// ===============================

esp_err_t ModbusRTU_Slave_Init(void);

#endif