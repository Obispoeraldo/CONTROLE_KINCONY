#ifndef RTC_DS1307_H
#define RTC_DS1307_H

#include <stdint.h>
#include "esp_err.h"

typedef struct
{
    uint8_t segundo;
    uint8_t minuto;
    uint8_t hora;
    uint8_t dia_semana;
    uint8_t dia;
    uint8_t mes;
    uint16_t ano;
} rtc_ds1307_t;

esp_err_t RTC_DS1307_Iniciar(void);
esp_err_t RTC_DS1307_LerHorario(rtc_ds1307_t *rtc);
esp_err_t RTC_DS1307_GravarHorario(const rtc_ds1307_t *rtc);

/*
 * Chame depois que o Wi-Fi estiver conectado.
 * Pega horário da internet e grava no DS1307.
 */
esp_err_t RTC_DS1307_AtualizarHorarioInternet(void);

#endif