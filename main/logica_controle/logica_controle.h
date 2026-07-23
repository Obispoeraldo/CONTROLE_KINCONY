#ifndef LOGICA_CONTROLE_H
#define LOGICA_CONTROLE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

/*
 * logica_controle.h
 *
 * Maquina de estado de operacao dos grupos motores.
 * Padrao inspirado no AquaPulse, adaptado para Kincony ESP-IDF.
 *
 * Regra principal:
 * - Entrada remoto/local = 1  -> permite controle das saidas.
 * - Entrada remoto/local = 0  -> modo monitoramento, bloqueia comandos.
 *
 * Cada chamador (MQTT, RTC/timers, painel Web, intertravamento, boot) informa sua propria
 * origem, armazenada em logica_grupos[grupo].origem ate o proximo comando aceito:
 *   Logica_Controle_SetComandoGrupo(LOGICA_GRUPO_1, true, ORIGEM_MQTT);
 *   Logica_Controle_SetComandoGrupo(LOGICA_GRUPO_1, false, ORIGEM_TIMER);
 */

#define LOGICA_CONTROLE_NUM_GRUPOS              5

/* Tempos padrao */
#define LOGICA_TIMEOUT_PARTIDA_MS               5000
#define LOGICA_TIMEOUT_PARADA_MS                5000
#define LOGICA_INTERVALO_ENTRE_PARTIDAS_MS      3000

#define LOGICA_USAR_ENTRADA_6_COMO_REMOTO       1

/*
 * Se 1: ao sair do modo remoto, o firmware desliga todas as saidas dos grupos.
 * Se 0: ao sair do modo remoto, ele apenas bloqueia novos comandos e monitora.
 */
#define LOGICA_DESLIGAR_AO_SAIR_REMOTO          1

typedef enum
{
    LOGICA_GRUPO_1 = 0,
    LOGICA_GRUPO_2,
    LOGICA_GRUPO_3,
    LOGICA_GRUPO_4,
    LOGICA_GRUPO_5,

    LOGICA_GRUPO_QTD
} logica_grupo_t;

typedef enum
{
    LOGICA_ESTADO_DESLIGADO = 0,
    LOGICA_ESTADO_AGUARDANDO_PARTIDA,
    LOGICA_ESTADO_LIGADO_OK,
    LOGICA_ESTADO_AGUARDANDO_PARADA,
    LOGICA_ESTADO_FALHA
} logica_estado_grupo_t;

typedef enum
{
    LOGICA_FALHA_NENHUMA = 0,
    LOGICA_FALHA_TIMEOUT_PARTIDA,
    LOGICA_FALHA_TIMEOUT_PARADA,
    LOGICA_FALHA_PERDA_FEEDBACK,
    LOGICA_FALHA_SAIDAS_OFFLINE,
    LOGICA_FALHA_ENTRADAS_OFFLINE
} logica_tipo_falha_t;

/*
 * Origem de quem provocou o ultimo comando aceito para o grupo. Armazenada uma unica vez,
 * no momento em que o comando e aceito (Logica_Controle_SetComandoGrupo/AlternarGrupo/
 * DesligarTodos ou a logica interna de intertravamento/boot) - a maquina de estados nunca
 * recalcula esse valor durante o processamento normal (processar_grupo() so consome).
 */
typedef enum
{
    ORIGEM_NENHUMA = 0,
    ORIGEM_MANUAL,
    ORIGEM_TIMER,
    ORIGEM_MQTT,
    ORIGEM_WEB,
    ORIGEM_INTERTRAVAMENTO,
    ORIGEM_RESTORE_BOOT
} origem_comando_t;

typedef struct
{
    logica_estado_grupo_t estado;
    logica_tipo_falha_t falha;

    bool comando_desejado;      /* pedido futuro do MQTT */
    bool comando_aplicado;      /* estado que a logica mandou para a saida */
    bool feedback;              /* leitura atual da entrada */
    bool aguardando_intervalo;  /* pedido de ligar esperando intervalo entre grupos */

    origem_comando_t origem;    /* quem provocou a ultima mudanca de comando_desejado/saida */

    /* Id do timer (0..RTC_TIMER_QUANTIDADE-1) que ligou este grupo e ainda pode desliga-lo ao
     * final da sua janela, ou -1 se nenhum timer o possui. So e alterado por
     * Logica_Controle_Timer*() (usadas exclusivamente por rtc_ds1307.c) e liberado automaticamente
     * por Logica_Controle_SetComandoGrupo() sempre que um comando com origem != ORIGEM_TIMER e
     * aceito - ver regra central no cabecalho deste arquivo. */
    int8_t timer_dono;

    TickType_t tick_estado;
    TickType_t tick_comando;
} logica_monitor_grupo_t;

extern logica_monitor_grupo_t logica_grupos[LOGICA_CONTROLE_NUM_GRUPOS];

extern bool logica_modo_remoto;
extern bool logica_falha_geral;
extern uint8_t logica_mascara_grupos_ligados;
extern uint8_t logica_mascara_falhas;
// Criado por Eraldo Bispo - 18/06/2026 22:17 - estado atual do alarme geral (saida 06)
extern bool logica_alarme_ativo;

esp_err_t Logica_Controle_Iniciar(void);
void Logica_Controle_Processar(void);

// Criado por Eraldo Bispo - 18/06/2026 22:17 - liga/desliga a saida 06 (alarme do controlador).
// Chamar a cada volta do loop principal, passando o status atual de MQTT e WiFi. Ver motivo
// completo no comentario da implementacao em logica_controle.c.
void Logica_Controle_AtualizarAlarme(bool mqtt_conectado, bool wifi_conectado);
bool Logica_Controle_IsAlarmeAtivo(void);

/* Funcoes que o MQTT, RTC/timers, painel Web etc. usam para comandar um grupo.
 * Todo chamador deve informar sua propria origem (ver origem_comando_t) - a funcao
 * armazena esse valor em logica_grupos[grupo].origem no momento em que o comando e aceito. */
esp_err_t Logica_Controle_SetComandoGrupo(logica_grupo_t grupo, bool ligar, origem_comando_t origem);
esp_err_t Logica_Controle_AlternarGrupo(logica_grupo_t grupo, origem_comando_t origem);
void Logica_Controle_DesligarTodos(origem_comando_t origem);
void Logica_Controle_ResetarFalhas(void);
void Logica_Controle_ResetarFalhaGrupo(logica_grupo_t grupo);

/* Consultas para debug, MQTT, Modbus ou printf no main */
bool Logica_Controle_IsRemoto(void);

/* True uma unica vez logo apos o modo remoto/local mudar (consome e reseta a flag). Usada pelo
 * MQTT para publicar o topico state imediatamente na troca de modo, sem depender so do intervalo
 * periodico de publicacao - ver Mqtt_Kincony_Processar() em mqtt_kincony.c. */
bool Logica_Controle_ConsumirMudancaModo(void);
bool Logica_Controle_IsFalhaGeral(void);
bool Logica_Controle_GetComandoGrupo(logica_grupo_t grupo);
bool Logica_Controle_GetFeedbackGrupo(logica_grupo_t grupo);
logica_estado_grupo_t Logica_Controle_GetEstadoGrupo(logica_grupo_t grupo);
logica_tipo_falha_t Logica_Controle_GetFalhaGrupo(logica_grupo_t grupo);
origem_comando_t Logica_Controle_GetOrigemGrupo(logica_grupo_t grupo);
uint8_t Logica_Controle_GetMascaraLigados(void);
uint8_t Logica_Controle_GetMascaraFalhas(void);
const char *Logica_Controle_EstadoToString(logica_estado_grupo_t estado);
const char *Logica_Controle_FalhaToString(logica_tipo_falha_t falha);

/* String padronizada para o payload MQTT (campo "src"): manual/timer/mqtt/web/interlock/restore/unknown. */
const char *Logica_Controle_OrigemToMqttString(origem_comando_t origem);

/* Controle de propriedade de grupo por timer - usado exclusivamente por rtc_ds1307.c para saber
 * se uma janela de timer especifica ainda tem o direito de desligar o grupo que ela ligou (evita
 * que um timer desligue um grupo assumido manualmente ou pertencente a outro timer - ver
 * docs/RELATORIO_TIMER_PROPRIEDADE.md). timer_id e o indice do timer (0..N-1) definido por quem
 * chama; -1 nunca deve ser passado como timer_id valido (e reservado para "nenhum dono"). */
void Logica_Controle_TimerAssumirPropriedade(logica_grupo_t grupo, int8_t timer_id);
bool Logica_Controle_TimerEhDono(logica_grupo_t grupo, int8_t timer_id);
void Logica_Controle_TimerLiberarPropriedade(logica_grupo_t grupo);

#endif
