#ifndef MODBUS_RS485_CONFIG_H
#define MODBUS_RS485_CONFIG_H

/*
 * modbus_rs485_config.h
 *
 * Criado por Eraldo Bispo - 23/06/2026
 *
 * Estruturas de configuracao do Modbus RTU master generico (RS485) e persistencia em NVS.
 * configuracao com N dispositivos Modbus, cada um com ate MODBUS_RS485_MAX_CANAIS_POR_DISPOSITIVO canais
 * configuraveis (function code, endereco, tipo de dado, escala, etc).
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "driver/uart.h"

#define MODBUS_RS485_MAX_DISPOSITIVOS            4
#define MODBUS_RS485_MAX_CANAIS_POR_DISPOSITIVO  4

typedef enum {
    MODBUS_TIPO_DADO_UINT16 = 0,
    MODBUS_TIPO_DADO_INT16,
    MODBUS_TIPO_DADO_UINT32,
    MODBUS_TIPO_DADO_INT32,
    MODBUS_TIPO_DADO_FLOAT32
} modbus_tipo_dado_t;

// Editado por Eraldo Bispo - 23/06/2026 - so importa para tipos de 32 bits (2 registradores);
// ABCD = big endian "puro" (registrador mais significativo primeiro, bytes na ordem natural).
typedef enum {
    MODBUS_ORDEM_PALAVRA_ABCD = 0,
    MODBUS_ORDEM_PALAVRA_CDAB,
    MODBUS_ORDEM_PALAVRA_BADC,
    MODBUS_ORDEM_PALAVRA_DCBA
} modbus_ordem_palavra_t;

typedef enum {
    MODBUS_FUNCAO_LEITURA_HOLDING = 0x03
} modbus_funcao_t;

typedef struct {
    bool habilitado;
    char nome[32];
    modbus_funcao_t funcao;
    uint16_t endereco_inicial;
    uint16_t quantidade_registradores;
    modbus_tipo_dado_t tipo_dado;
    modbus_ordem_palavra_t ordem_palavra;
    float escala;
    float deslocamento;
    uint8_t casas_decimais;
    char unidade[8];
    bool limite_minimo_habilitado;
    float limite_minimo;
    bool limite_maximo_habilitado;
    float limite_maximo;
    char campo_mqtt[24];
    uint32_t ciclo_leitura_ms;

    /* Estado em runtime - nao persistido em NVS, recalculado a cada leitura */
    float ultimo_valor;
    bool ultimo_valor_valido;
    int64_t timestamp_ultimo_valor_ms;

} modbus_canal_t;

typedef struct {
    bool habilitado;
    char nome[32];
    char identificador_mqtt[24];
    uint8_t endereco_escravo;
    uint32_t timeout_ms;
    modbus_canal_t canais[MODBUS_RS485_MAX_CANAIS_POR_DISPOSITIVO];

    /* Estado em runtime - nao persistido em NVS */
    bool online;
    uint32_t contador_sucesso;
    uint32_t contador_falha_consecutiva;
    // Criado por Eraldo Bispo - 23/06/2026 - sucessos consecutivos desde a ultima falha, usado
    // para decidir quando voltar a "online" (limite_sucessos_recuperacao), separado do contador
    // de sucesso total acima.
    uint32_t contador_sucesso_consecutivo;
    uint32_t contador_falha_total;
    uint32_t contador_timeout;
    uint32_t contador_exception;
    int64_t timestamp_ultima_comunicacao_ms;
    int64_t timestamp_ultima_tentativa_ms;
    esp_err_t ultimo_erro;
} modbus_dispositivo_t;

typedef struct {
    bool habilitado;
    uint32_t baud_rate;
    uart_parity_t paridade;
    uint8_t bits_dados;
    uint8_t bits_parada;
    uint32_t timeout_resposta_ms;
    uint32_t intervalo_entre_requisicoes_ms;
    uint8_t quantidade_tentativas;
    uint32_t limite_falhas_offline;
    uint32_t limite_sucessos_recuperacao;
    modbus_dispositivo_t dispositivos[MODBUS_RS485_MAX_DISPOSITIVOS];
} modbus_rs485_configuracao_t;

// Le a configuracao gravada em NVS (namespace "modbus_rs485", uma chave para o barramento e
// uma chave por dispositivo - ver motivo em modbus_rs485_config.c). Se nao existir ainda,
// grava e devolve a configuracao padrao de fabrica.
esp_err_t Modbus_RS485_Config_Carregar(modbus_rs485_configuracao_t *destino);

// Grava a configuracao em NVS. Nao reinicia sozinho - quem chamar decide se/quando
// reiniciar o Modbus_RS485 para a mudanca valer (so relevante a partir da Fase 2, com API).
esp_err_t Modbus_RS485_Config_Salvar(const modbus_rs485_configuracao_t *config);

// Monta a string "8N1"/"8E1"/"8O1" (bits_dados + paridade + bits_parada) a partir da config,
// usada no payload MQTT config-rs485 ("Par"). destino deve ter pelo menos 4 bytes.
void Modbus_RS485_Config_FormatarParidade(const modbus_rs485_configuracao_t *config, char *destino, size_t tamanho_destino);

#endif
