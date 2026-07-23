/*
 * rtc_ds1307.h
 *
 * DS1307 (com bateria) + 4 timers persistentes via NVS.
 *
 * IMPORTANTE:
 * - Este modulo NAO inicializa o barramento I2C.
 * - Usa I2C_NUM_0, que deve estar previamente iniciado pelo driver
 *   de entradas da Kincony.
 * - Nem toda variante/placa tem o chip DS1307 fisico soldado (ver nota em
 *   ntp_kincony.h). RTC_DS1307_Iniciar() detecta a ausencia do chip e
 *   desativa graciosamente os recursos de RTC/timers sem falhar o boot.
 *
 * Editado por Eraldo Bispo e Daniel Montanher - integracao (AquaPulse
 * v0.2.0-integration.1) - servico de tempo unificado com main/ntp/ntp_kincony.c:
 * o DS1307 fornece a hora no boot (antes do WiFi/NTP existirem) e a mantem
 * durante quedas de energia/internet; o NTP (ntp_kincony.c, cliente SNTP
 * unico do projeto) corrige o relogio do sistema quando ha internet e chama
 * RTC_DS1307_NotificarSincronizacaoNtp() para gravar a hora corrigida de
 * volta no DS1307. Ver docs/RELATORIO_INTEGRACAO_ERALDO_DANIEL.md.
 */

#ifndef RTC_DS1307_H
#define RTC_DS1307_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/time.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

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

/*
 * Detecta o DS1307, carrega os timers da NVS e aplica a hora do RTC ao ESP32.
 * Retorna ESP_OK mesmo quando o chip nao responde no I2C (placa sem RTC
 * fisico): nesse caso os timers/RTC ficam inativos, mas o boot continua
 * normalmente. So retorna erro em falha de alocacao (mutex). Chame apos o
 * I2C estar instalado (Entradas_Kincony_Iniciar()) e antes do NTP.
 */
esp_err_t RTC_DS1307_Iniciar(void);

/* Le e grava data/hora no DS1307. */
esp_err_t RTC_DS1307_LerHorario(rtc_ds1307_t *rtc);
esp_err_t RTC_DS1307_GravarHorario(const rtc_ds1307_t *rtc);

/*
 * Chamada pelo callback de sincronizacao do cliente SNTP unico do projeto
 * (config.sync_cb em Ntp_Kincony_Iniciar(), ver ntp_kincony.c) toda vez que
 * o NTP confirma a hora. Grava a hora corrigida no DS1307 (se o chip estiver
 * presente) para que ela sobreviva a queda de energia/reboot sem internet.
 * Nao bloqueia por muito tempo (I2C com timeout de 100 ms), mas nao deve ser
 * chamada em contexto de interrupcao.
 */
void RTC_DS1307_NotificarSincronizacaoNtp(const struct timeval *tv);

/* true se o DS1307 respondeu no I2C durante RTC_DS1307_Iniciar(). */
bool RTC_DS1307_EstaDisponivel(void);

/*
 * Processa timer_set, timer_enable, timer_disable, timer_clear,
 * timer_clear_all, timer_get, rtc_get e rtc_sync.
 *
 * Retornos:
 * - ESP_OK: comando tratado com sucesso;
 * - ESP_ERR_INVALID_ARG: comando reconhecido, mas com dados invalidos;
 * - ESP_ERR_NOT_SUPPORTED: o JSON nao pertence ao RTC/timers.
 */
esp_err_t RTC_DS1307_ProcessarComandoMQTT(
    const char *payload,
    char *resposta,
    size_t tamanho_resposta
);

/*
 * Deve ser chamada continuamente no loop principal.
 * A funcao le o RTC aproximadamente uma vez por segundo e executa
 * os eventos dos quatro timers apenas uma vez por minuto.
 */
void RTC_DS1307_Processar(void);

#ifdef __cplusplus
}
#endif

#endif /* RTC_DS1307_H */
