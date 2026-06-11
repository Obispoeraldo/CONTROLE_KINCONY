#ifndef ENTRADAS_KINCONY_H
#define ENTRADAS_KINCONY_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#define ENTRADAS_KINCONY_ADDR        0x24
#define ENTRADAS_KINCONY_QTD         8

typedef enum
{
    ENTRADA_1 = 0,
    ENTRADA_2,
    ENTRADA_3,
    ENTRADA_4,
    ENTRADA_5,
    ENTRADA_6,
    ENTRADA_7,
    ENTRADA_8
} entrada_kincony_t;

esp_err_t Entradas_Kincony_Init(i2c_master_bus_handle_t i2c_bus);
esp_err_t Entradas_Kincony_Atualizar(void);

bool Entradas_Kincony_Get(entrada_kincony_t entrada);
uint8_t Entradas_Kincony_GetRaw(void);
uint8_t Entradas_Kincony_GetEstadoTratado(void);

bool Entradas_Kincony_IsOnline(void);

#endif