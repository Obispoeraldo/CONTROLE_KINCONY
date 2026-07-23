#ifndef WIFI_KINCONY_H
#define WIFI_KINCONY_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

/*
 * wifi_kincony.h
 *
 * Maquina de estados de conexao WiFi, nao bloqueante:
 * - reconexao permanente (sem timeout global) com intervalo progressivo entre
 *   tentativas (30s -> 60s -> 5 min), nunca desiste da rede salva;
 * - Portal de configuracao (Access Point "Kincony-Config") que pode ser aberto a
 *   qualquer momento via Wifi_Kincony_AbrirPortalConfiguracao(), sem depender do
 *   encerramento das tentativas de reconexao (WIFI_MODE_APSTA - o STA continua
 *   tentando em paralelo);
 * - abertura automatica do portal apos ficar desconectado por muito tempo (ver
 *   WIFI_KINCONY_AUTO_AP_LIMIAR_MS), fechado sozinho quando a rede volta (a menos
 *   que tenha sido aberto manualmente pelo botao/painel - nesse caso so fecha por
 *   acao explicita);
 * - teste de uma rede nova em RAM (Wifi_Kincony_TestarNovaRede), sem gravar nada
 *   em NVS ate confirmar que a conexao realmente funciona.
 *
 * Ver docs/RELATORIO_WIFI_RECONEXAO.md para o diagnostico da versao anterior
 * (preservada em docs/wifi_reconexao_v1_legado/) e a arquitetura completa.
 */

// Editado por Eraldo Bispo - sinaliza falha inicial ao main.c (para o mecanismo de restaurar
// backup no boot) apos este numero de tentativas rapidas. Depois disso o modulo CONTINUA
// tentando indefinidamente com intervalo progressivo - nunca desiste. Ver wifi_kincony.c.
#define WIFI_KINCONY_TENTATIVAS_FALHA_INICIAL  3

typedef enum
{
    WIFI_KINCONY_ESTADO_SEM_CREDENCIAIS = 0,
    WIFI_KINCONY_ESTADO_INICIANDO_CONEXAO,
    WIFI_KINCONY_ESTADO_AGUARDANDO_CONEXAO,
    WIFI_KINCONY_ESTADO_CONECTADO,
    WIFI_KINCONY_ESTADO_DESCONECTADO,
    WIFI_KINCONY_ESTADO_AGUARDANDO_NOVA_TENTATIVA,
    WIFI_KINCONY_ESTADO_PORTAL_CONFIGURACAO_ATIVO,
    WIFI_KINCONY_ESTADO_VALIDANDO_NOVA_REDE,
    WIFI_KINCONY_ESTADO_ERRO_NOVA_REDE
} wifi_kincony_estado_t;

esp_err_t Wifi_Kincony_Init(const char *ssid, const char *senha);

// Chamar a cada volta do loop principal. Nao bloqueia - so verifica o temporizador interno
// de abertura automatica do portal apos desconexao prolongada.
void Wifi_Kincony_Processar(void);

// Espera curta (bounded) so para o log de boot informar se a conexao inicial foi rapida ou
// nao. NAO decide mais se o modulo "desiste" da rede - a reconexao continua para sempre em
// segundo plano independente do retorno desta funcao.
bool Wifi_Kincony_EsperarResultado(uint32_t timeout_ms);

bool Wifi_Kincony_IsConectado(void);
bool Wifi_Kincony_IsFalha(void);
wifi_kincony_estado_t Wifi_Kincony_GetEstado(void);
const char *Wifi_Kincony_EstadoToString(wifi_kincony_estado_t estado);

// Criado por Eraldo Bispo - copia o IP atual da interface STA (ex: "192.168.1.3") para destino.
// Se nao estiver conectado, escreve string vazia (nunca "0.0.0.0").
void Wifi_Kincony_GetIpAtual(char *destino, size_t tamanho);

uint8_t Wifi_Kincony_GetTentativas(void);

esp_err_t Wifi_Kincony_Reconectar(void);
esp_err_t Wifi_Kincony_Desconectar(void);

// ---- Portal de configuracao (Access Point) ----
// Idempotente: chamar varias vezes nao recria nem reinicia nada - so alterna o modo WiFi
// entre STA e APSTA (a interface AP e criada e configurada uma unica vez em
// Wifi_Kincony_Init(), evitando "criacao repetida de Access Point").
// manual=true: aberto pelo botao fisico ou pelo painel - so fecha por acao explicita.
// manual=false: aberto automaticamente por desconexao prolongada - fecha sozinho assim que
// a rede salva reconectar.
esp_err_t Wifi_Kincony_AbrirPortalConfiguracao(bool manual);
esp_err_t Wifi_Kincony_FecharPortalConfiguracao(void);
bool Wifi_Kincony_PortalAtivo(void);

// Mantido por compatibilidade - alias de Wifi_Kincony_AbrirPortalConfiguracao(true).
esp_err_t Wifi_Kincony_IniciarModoEmergenciaAP(void);

// ---- Teste/validacao de uma rede nova, sem tocar em NVS ----
// Troca a config STA em RAM para (ssid, senha), tenta conectar e espera ate timeout_ms.
// Se falhar, restaura automaticamente a config STA anterior (a que estava rodando antes do
// teste) e reconecta a ela - as credenciais anteriores nunca ficam inacessiveis. Quem chama
// decide o que fazer com o resultado (ver config_server_kincony.c, handler_post_config) -
// esta funcao NUNCA grava em NVS.
// Bloqueia apenas quem chamou (tipicamente a task do servidor HTTP, independente da task
// principal) por ate timeout_ms - nao afeta o loop principal nem outras tasks.
bool Wifi_Kincony_TestarNovaRede(const char *ssid, const char *senha, uint32_t timeout_ms);

#endif
