#ifndef MODBUS_RTU_SLAVE_ESP_H
#define MODBUS_RTU_SLAVE_ESP_H

#include <stdint.h>
#include "esp_err.h"

// ===============================
// CONFIGURAÇÃO DO MODBUS
// ===============================

#define MB_RTU_SLAVE_ID        1
#define MB_RTU_UART_PORT       UART_NUM_2
#define MB_RTU_TX_PIN          17
#define MB_RTU_RX_PIN          16
#define MB_RTU_DE_RE_PIN       2
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