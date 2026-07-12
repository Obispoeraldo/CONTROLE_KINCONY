#ifndef NTP_KINCONY_H
#define NTP_KINCONY_H

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

/*
 * ntp_kincony.h
 *
 * Criado por Eraldo Bispo - 24/06/2026
 * Integrado por Eraldo Bispo e Daniel Montanher - servico de tempo unificado
 * (AquaPulse v0.2.0-integration.1)
 *
 * Sincroniza data/hora pela internet (SNTP, esp_netif_sntp_init/start). Este
 * e o UNICO cliente SNTP do projeto: em placas com o RTC DS1307 fisico (ver
 * main/rtc/rtc_ds1307.h, implementado por Daniel Montanher), cada sincronizacao
 * bem-sucedida tambem corrige o DS1307 via RTC_DS1307_NotificarSincronizacaoNtp()
 * (registrada como "sync_cb" em Ntp_Kincony_Iniciar()), para a hora sobreviver
 * a queda de energia/internet. Em placas SEM o DS1307, a hora so fica
 * disponivel apos a primeira sincronizacao NTP (ver
 * Ntp_Kincony_ObterDataHoraFormatada()).
 *
 * Fuso horario fixo (sem horario de verao - abolido no Brasil desde 2019),
 * aplicado via "TZ" (setenv + tzset) no formato POSIX usado nos exemplos
 * oficiais de SNTP do ESP-IDF (ex: fuso -3 -> TZ="<-03>3").
 *
 * Chamar Ntp_Kincony_Iniciar() uma vez, depois que o WiFi conectou (SNTP
 * precisa de rede; nao bloqueia o boot se a sincronizacao demorar).
 *
 * IMPORTANTE: nao usar a API legada "esp_sntp.h" (esp_sntp_init/esp_sntp_stop
 * etc.) em nenhum outro modulo deste projeto - ela controla o mesmo cliente
 * SNTP global do lwIP que este modulo gerencia via esp_netif_sntp_*(). Para
 * forcar uma nova sincronizacao (ex: comando MQTT "rtc_sync"), use
 * Ntp_Kincony_ForcarResincronizacao().
 */

esp_err_t Ntp_Kincony_Iniciar(const char *servidor, int8_t fuso_horas);

// Formata a hora local atual como "DD/MM/AAAA HH:MM:SS" em destino. Se a
// hora do sistema ainda nao foi sincronizada (ano < 2024), escreve
// "Sincronizando..." no lugar.
void Ntp_Kincony_ObterDataHoraFormatada(char *destino, size_t tamanho);

// Criado por Eraldo Bispo e Daniel Montanher - integracao - forca uma nova
// rodada de sincronizacao no cliente SNTP ja inicializado (nao bloqueia; o
// resultado chega de forma assincrona pelo mesmo "sync_cb" de sempre). Usado
// pelo comando MQTT "rtc_sync" (ver RTC_DS1307_ProcessarComandoMQTT()).
// Retorna ESP_ERR_INVALID_STATE se Ntp_Kincony_Iniciar() ainda nao rodou.
esp_err_t Ntp_Kincony_ForcarResincronizacao(void);

#endif
