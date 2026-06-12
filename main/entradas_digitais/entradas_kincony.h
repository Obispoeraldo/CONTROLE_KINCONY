#ifndef ENTRADAS_KINCONY_H
#define ENTRADAS_KINCONY_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

typedef enum
{
    GRUPO_MOTOR1 = 0,
    GRUPO_MOTOR2,
    GRUPO_MOTOR3,
    GRUPO_MOTOR4,
    GRUPO_MOTOR5,
    GRUPO_MOTOR6,
    CHAVE_REMOTO,
    ENTRADA_8
} entrada_kincony_t;

extern uint8_t entrada_1;
extern uint8_t entrada_2;
extern uint8_t entrada_3;
extern uint8_t entrada_4;
extern uint8_t entrada_5;
extern uint8_t entrada_6;
extern uint8_t entrada_7;
extern uint8_t entrada_8;

extern uint8_t grupo_motor1;
extern uint8_t grupo_motor2;
extern uint8_t grupo_motor3;
extern uint8_t grupo_motor4;
extern uint8_t grupo_motor5;
extern uint8_t grupo_motor6;
extern uint8_t chave_remoto;

esp_err_t Entradas_Kincony_Iniciar(void);
void Entradas_Kincony_Processar(void);

uint8_t Entradas_Kincony_Get(entrada_kincony_t entrada);
uint8_t Entradas_Kincony_GetEstado(void);
bool Entradas_Kincony_IsOnline(void);

#endif