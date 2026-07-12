#ifndef NTP_KINCONY_H
#define NTP_KINCONY_H

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

/*
 * ntp_kincony.h
 *
 * Criado por Eraldo Bispo - 24/06/2026
 *
 * Sincroniza data/hora pela internet (SNTP, esp_netif_sntp_init/start) - o
 * KC868-A6 nao tem RTC com bateria no projeto hoje, entao nao existe ajuste
 * manual: sem rede/NTP, a hora fica indisponivel (ver
 * Ntp_Kincony_ObterDataHoraFormatada()) ate a proxima sincronizacao.
 *
 * Fuso horario fixo (sem horario de verao - abolido no Brasil desde 2019),
 * aplicado via "TZ" (setenv + tzset) no formato POSIX usado nos exemplos
 * oficiais de SNTP do ESP-IDF (ex: fuso -3 -> TZ="<-03>3").
 *
 * Chamar Ntp_Kincony_Iniciar() uma vez, depois que o WiFi conectou (SNTP
 * precisa de rede; nao bloqueia o boot se a sincronizacao demorar).
 */

esp_err_t Ntp_Kincony_Iniciar(const char *servidor, int8_t fuso_horas);

// Formata a hora local atual como "DD/MM/AAAA HH:MM:SS" em destino. Se a
// hora do sistema ainda nao foi sincronizada (ano < 2024), escreve
// "Sincronizando..." no lugar.
void Ntp_Kincony_ObterDataHoraFormatada(char *destino, size_t tamanho);

#endif
