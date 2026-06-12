#ifndef SAIDAS_DIGITAIS_KINCONY_H
#define SAIDAS_DIGITAIS_KINCONY_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#define SAIDAS_PCF_ADDR  0x24

typedef enum
{
    SAIDA_1 = 0,
    SAIDA_2,
    SAIDA_3,
    SAIDA_4,
    SAIDA_5,
    SAIDA_6,
    SAIDA_7,
    SAIDA_8,

    SAIDAS_KINCONY_QTD
} saida_kincony_t;

esp_err_t Saidas_Kincony_Iniciar(void);

esp_err_t Saidas_Kincony_Ligar(saida_kincony_t saida);
esp_err_t Saidas_Kincony_Desligar(saida_kincony_t saida);
esp_err_t Saidas_Kincony_DesligarTodas(void);

uint8_t Saidas_Kincony_GetEstado(void);
bool Saidas_Kincony_Get(saida_kincony_t saida);
bool Saidas_Kincony_IsOnline(void);

#endif