/*
 * modbus_rs485_config.c
 *
 * Criado por Eraldo Bispo - 23/06/2026
 */

#include "modbus_rs485_config.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "cJSON.h"

static const char *TAG = "MODBUS_RS485_CFG";

#define NVS_NAMESPACE   "modbus_rs485"
#define NVS_CHAVE_BARRAMENTO "barramento"

// Editado por Eraldo Bispo - 23/06/2026 - tamanho maximo de UM valor JSON gravado em NVS (o
// barramento, ou 1 dispositivo com seus 4 canais).
#define MODBUS_RS485_JSON_TAM_MAX 2048

static void canal_para_json(cJSON *array_canais, const modbus_canal_t *canal)
{
    cJSON *item = cJSON_CreateObject();

    cJSON_AddBoolToObject(item, "habilitado", canal->habilitado);
    cJSON_AddStringToObject(item, "nome", canal->nome);
    cJSON_AddNumberToObject(item, "funcao", (double)canal->funcao);
    cJSON_AddNumberToObject(item, "endereco_inicial", canal->endereco_inicial);
    cJSON_AddNumberToObject(item, "quantidade_registradores", canal->quantidade_registradores);
    cJSON_AddNumberToObject(item, "tipo_dado", (double)canal->tipo_dado);
    cJSON_AddNumberToObject(item, "ordem_palavra", (double)canal->ordem_palavra);
    cJSON_AddNumberToObject(item, "escala", canal->escala);
    cJSON_AddNumberToObject(item, "deslocamento", canal->deslocamento);
    cJSON_AddNumberToObject(item, "casas_decimais", canal->casas_decimais);
    cJSON_AddStringToObject(item, "unidade", canal->unidade);
    cJSON_AddBoolToObject(item, "limite_minimo_habilitado", canal->limite_minimo_habilitado);
    cJSON_AddNumberToObject(item, "limite_minimo", canal->limite_minimo);
    cJSON_AddBoolToObject(item, "limite_maximo_habilitado", canal->limite_maximo_habilitado);
    cJSON_AddNumberToObject(item, "limite_maximo", canal->limite_maximo);
    cJSON_AddStringToObject(item, "campo_mqtt", canal->campo_mqtt);
    cJSON_AddNumberToObject(item, "ciclo_leitura_ms", canal->ciclo_leitura_ms);

    cJSON_AddItemToArray(array_canais, item);
}

static void canal_de_json(const cJSON *item, modbus_canal_t *canal)
{
    const cJSON *campo;

    memset(canal, 0, sizeof(*canal));

    if ((campo = cJSON_GetObjectItem(item, "habilitado")) != NULL) canal->habilitado = cJSON_IsTrue(campo);
    if ((campo = cJSON_GetObjectItem(item, "nome")) != NULL && cJSON_IsString(campo)) strncpy(canal->nome, campo->valuestring, sizeof(canal->nome) - 1);
    if ((campo = cJSON_GetObjectItem(item, "funcao")) != NULL) canal->funcao = (modbus_funcao_t)campo->valueint;
    if ((campo = cJSON_GetObjectItem(item, "endereco_inicial")) != NULL) canal->endereco_inicial = (uint16_t)campo->valueint;
    if ((campo = cJSON_GetObjectItem(item, "quantidade_registradores")) != NULL) canal->quantidade_registradores = (uint16_t)campo->valueint;
    if ((campo = cJSON_GetObjectItem(item, "tipo_dado")) != NULL) canal->tipo_dado = (modbus_tipo_dado_t)campo->valueint;
    if ((campo = cJSON_GetObjectItem(item, "ordem_palavra")) != NULL) canal->ordem_palavra = (modbus_ordem_palavra_t)campo->valueint;
    if ((campo = cJSON_GetObjectItem(item, "escala")) != NULL) canal->escala = (float)campo->valuedouble;
    if ((campo = cJSON_GetObjectItem(item, "deslocamento")) != NULL) canal->deslocamento = (float)campo->valuedouble;
    if ((campo = cJSON_GetObjectItem(item, "casas_decimais")) != NULL) canal->casas_decimais = (uint8_t)campo->valueint;
    if ((campo = cJSON_GetObjectItem(item, "unidade")) != NULL && cJSON_IsString(campo)) strncpy(canal->unidade, campo->valuestring, sizeof(canal->unidade) - 1);
    if ((campo = cJSON_GetObjectItem(item, "limite_minimo_habilitado")) != NULL) canal->limite_minimo_habilitado = cJSON_IsTrue(campo);
    if ((campo = cJSON_GetObjectItem(item, "limite_minimo")) != NULL) canal->limite_minimo = (float)campo->valuedouble;
    if ((campo = cJSON_GetObjectItem(item, "limite_maximo_habilitado")) != NULL) canal->limite_maximo_habilitado = cJSON_IsTrue(campo);
    if ((campo = cJSON_GetObjectItem(item, "limite_maximo")) != NULL) canal->limite_maximo = (float)campo->valuedouble;
    if ((campo = cJSON_GetObjectItem(item, "campo_mqtt")) != NULL && cJSON_IsString(campo)) strncpy(canal->campo_mqtt, campo->valuestring, sizeof(canal->campo_mqtt) - 1);
    if ((campo = cJSON_GetObjectItem(item, "ciclo_leitura_ms")) != NULL) canal->ciclo_leitura_ms = (uint32_t)campo->valuedouble;
}

// Editado por Eraldo Bispo - 23/06/2026 - monta o objeto JSON de UM dispositivo (com seus
// canais), sem adiciona-lo a nenhum array: cada dispositivo agora vira sua propria chave NVS
static void dispositivo_para_json(cJSON *item, const modbus_dispositivo_t *dispositivo)
{
    cJSON *canais = cJSON_CreateArray();

    cJSON_AddBoolToObject(item, "habilitado", dispositivo->habilitado);
    cJSON_AddStringToObject(item, "nome", dispositivo->nome);
    cJSON_AddStringToObject(item, "identificador_mqtt", dispositivo->identificador_mqtt);
    cJSON_AddNumberToObject(item, "endereco_escravo", dispositivo->endereco_escravo);
    cJSON_AddNumberToObject(item, "timeout_ms", dispositivo->timeout_ms);

    for (uint8_t i = 0; i < MODBUS_RS485_MAX_CANAIS_POR_DISPOSITIVO; i++)
    {
        canal_para_json(canais, &dispositivo->canais[i]);
    }

    cJSON_AddItemToObject(item, "canais", canais);
}

static void dispositivo_de_json(const cJSON *item, modbus_dispositivo_t *dispositivo)
{
    const cJSON *campo;
    const cJSON *canais;

    memset(dispositivo, 0, sizeof(*dispositivo));

    if ((campo = cJSON_GetObjectItem(item, "habilitado")) != NULL) dispositivo->habilitado = cJSON_IsTrue(campo);
    if ((campo = cJSON_GetObjectItem(item, "nome")) != NULL && cJSON_IsString(campo)) strncpy(dispositivo->nome, campo->valuestring, sizeof(dispositivo->nome) - 1);
    if ((campo = cJSON_GetObjectItem(item, "identificador_mqtt")) != NULL && cJSON_IsString(campo)) strncpy(dispositivo->identificador_mqtt, campo->valuestring, sizeof(dispositivo->identificador_mqtt) - 1);
    if ((campo = cJSON_GetObjectItem(item, "endereco_escravo")) != NULL) dispositivo->endereco_escravo = (uint8_t)campo->valueint;
    if ((campo = cJSON_GetObjectItem(item, "timeout_ms")) != NULL) dispositivo->timeout_ms = (uint32_t)campo->valuedouble;

    canais = cJSON_GetObjectItem(item, "canais");

    if (canais != NULL && cJSON_IsArray(canais))
    {
        int indice = 0;
        const cJSON *canal_item;

        cJSON_ArrayForEach(canal_item, canais)
        {
            if (indice >= MODBUS_RS485_MAX_CANAIS_POR_DISPOSITIVO)
            {
                break;
            }

            canal_de_json(canal_item, &dispositivo->canais[indice]);
            indice++;
        }
    }
}

// Editado por Eraldo Bispo - 30/06/2026 - configuracao padrao de fabrica: dispositivo 0 e o
// Novus N1200 (mapa confirmado no manual: holding 0x0000=SP, 0x0001=PV, ambos uint16 x10).
// Dispositivo 1 e a sonda Renkeer RS-LDO-N01-EX com o mapa real do datasheet V1.9 (secao 2.4.3):
//   0x0000-0x0001 saturacao O2 (float32 big endian, raw 0.0-1.0 = 0-100%, escala x100)
//   0x0002-0x0003 concentracao O2 (float32 big endian, ja em mg/L, escala x1)
//   0x0004-0x0005 temperatura interna (float32 big endian, ja em graus C, escala x1)
// ATENCAO: o baud rate padrao de fabrica do RS-LDO-N01-EX e 4800 baud. Configurar o sensor
// para 9600 baud via software Renke (tool de parametros 485) antes de ligar no barramento,
// ou alterar o barramento para 4800 neste projeto. Os dois nao podem operar em baud diferentes.
static void carregar_padrao_fabrica(modbus_rs485_configuracao_t *config)
{
    memset(config, 0, sizeof(*config));

    config->habilitado = true;
    config->baud_rate = 9600;
    config->paridade = UART_PARITY_DISABLE;
    config->bits_dados = 8; /* numero literal de bits, nao o enum uart_word_length_t */
    config->bits_parada = 1; /* numero literal de stop bits, nao o enum uart_stop_bits_t */
    config->timeout_resposta_ms = 1500;
    config->intervalo_entre_requisicoes_ms = 10;
    config->quantidade_tentativas = 2;
    config->limite_falhas_offline = 3;
    config->limite_sucessos_recuperacao = 1;

    modbus_dispositivo_t *novus = &config->dispositivos[0];
    novus->habilitado = true;
    strncpy(novus->nome, "Novus N1200 (teste serial)", sizeof(novus->nome) - 1);
    strncpy(novus->identificador_mqtt, "novus", sizeof(novus->identificador_mqtt) - 1);
    novus->endereco_escravo = 5;
    novus->timeout_ms = 0;

    modbus_canal_t *novus_sp = &novus->canais[0];
    novus_sp->habilitado = true;
    strncpy(novus_sp->nome, "SP ativo", sizeof(novus_sp->nome) - 1);
    novus_sp->funcao = MODBUS_FUNCAO_LEITURA_HOLDING;
    novus_sp->endereco_inicial = 0x0000;
    novus_sp->quantidade_registradores = 1;
    novus_sp->tipo_dado = MODBUS_TIPO_DADO_UINT16;
    novus_sp->escala = 0.1f; /* registrador vem x10, conforme manual do Novus N1200 */
    novus_sp->casas_decimais = 1;
    strncpy(novus_sp->campo_mqtt, "setpoint", sizeof(novus_sp->campo_mqtt) - 1);
    novus_sp->ciclo_leitura_ms = 2000;

    modbus_canal_t *novus_pv = &novus->canais[1];
    novus_pv->habilitado = true;
    strncpy(novus_pv->nome, "PV (temperatura)", sizeof(novus_pv->nome) - 1);
    novus_pv->funcao = MODBUS_FUNCAO_LEITURA_HOLDING;
    novus_pv->endereco_inicial = 0x0001;
    novus_pv->quantidade_registradores = 1;
    novus_pv->tipo_dado = MODBUS_TIPO_DADO_UINT16;
    novus_pv->escala = 0.1f;
    novus_pv->casas_decimais = 1;
    strncpy(novus_pv->campo_mqtt, "temperatura", sizeof(novus_pv->campo_mqtt) - 1);
    novus_pv->ciclo_leitura_ms = 2000;

    // Editado por Eraldo Bispo - 30/06/2026 - mapa real da Renkeer RS-LDO-N01-EX (datasheet V1.9,
    // secao 2.4.3). Tres canais de leitura: saturacao, concentracao e temperatura.
    // O sensor usa float32 big endian (palavra mais significativa no primeiro registrador = ABCD).
    modbus_dispositivo_t *renkeer = &config->dispositivos[1];
    renkeer->habilitado = true;
    strncpy(renkeer->nome, "Sonda Renkeer RS-LDO", sizeof(renkeer->nome) - 1);
    strncpy(renkeer->identificador_mqtt, "sonda01", sizeof(renkeer->identificador_mqtt) - 1);
    renkeer->endereco_escravo = 1; /* endereco padrao de fabrica do RS-LDO-N01-EX */
    renkeer->timeout_ms = 2000;    /* sensor fluorescente - resposta pode ser mais lenta */

    /* Canal 0: saturacao de O2 - reg 0x0000-0x0001, float32 big endian.
     * Raw 0.0 a 1.0 representa 0% a 100% de saturacao. Escala x100 para exibir em %. */
    modbus_canal_t *renkeer_sat = &renkeer->canais[0];
    renkeer_sat->habilitado = true;
    strncpy(renkeer_sat->nome, "Saturacao O2", sizeof(renkeer_sat->nome) - 1);
    renkeer_sat->funcao = MODBUS_FUNCAO_LEITURA_HOLDING;
    renkeer_sat->endereco_inicial = 0x0000;
    renkeer_sat->quantidade_registradores = 2;
    renkeer_sat->tipo_dado = MODBUS_TIPO_DADO_FLOAT32;
    renkeer_sat->ordem_palavra = MODBUS_ORDEM_PALAVRA_ABCD; /* big endian, conforme datasheet */
    renkeer_sat->escala = 100.0f;
    renkeer_sat->deslocamento = 0.0f;
    renkeer_sat->casas_decimais = 1;
    strncpy(renkeer_sat->unidade, "%sat", sizeof(renkeer_sat->unidade) - 1);
    strncpy(renkeer_sat->campo_mqtt, "saturacao", sizeof(renkeer_sat->campo_mqtt) - 1);
    renkeer_sat->ciclo_leitura_ms = 5000;

    /* Canal 1: concentracao de O2 - reg 0x0002-0x0003, float32 big endian, ja em mg/L. */
    modbus_canal_t *renkeer_o2 = &renkeer->canais[1];
    renkeer_o2->habilitado = true;
    strncpy(renkeer_o2->nome, "Oxigenio dissolvido", sizeof(renkeer_o2->nome) - 1);
    renkeer_o2->funcao = MODBUS_FUNCAO_LEITURA_HOLDING;
    renkeer_o2->endereco_inicial = 0x0002;
    renkeer_o2->quantidade_registradores = 2;
    renkeer_o2->tipo_dado = MODBUS_TIPO_DADO_FLOAT32;
    renkeer_o2->ordem_palavra = MODBUS_ORDEM_PALAVRA_ABCD;
    renkeer_o2->escala = 1.0f;
    renkeer_o2->deslocamento = 0.0f;
    renkeer_o2->casas_decimais = 2;
    strncpy(renkeer_o2->unidade, "mg/L", sizeof(renkeer_o2->unidade) - 1);
    strncpy(renkeer_o2->campo_mqtt, "oxigenio", sizeof(renkeer_o2->campo_mqtt) - 1);
    renkeer_o2->ciclo_leitura_ms = 5000;

    /* Canal 2: temperatura interna - reg 0x0004-0x0005, float32 big endian, ja em graus C. */
    modbus_canal_t *renkeer_temp = &renkeer->canais[2];
    renkeer_temp->habilitado = true;
    strncpy(renkeer_temp->nome, "Temperatura", sizeof(renkeer_temp->nome) - 1);
    renkeer_temp->funcao = MODBUS_FUNCAO_LEITURA_HOLDING;
    renkeer_temp->endereco_inicial = 0x0004;
    renkeer_temp->quantidade_registradores = 2;
    renkeer_temp->tipo_dado = MODBUS_TIPO_DADO_FLOAT32;
    renkeer_temp->ordem_palavra = MODBUS_ORDEM_PALAVRA_ABCD;
    renkeer_temp->escala = 1.0f;
    renkeer_temp->deslocamento = 0.0f;
    renkeer_temp->casas_decimais = 1;
    strncpy(renkeer_temp->unidade, "\xC2\xB0""C", sizeof(renkeer_temp->unidade) - 1); /* °C UTF-8 */
    strncpy(renkeer_temp->campo_mqtt, "temperatura", sizeof(renkeer_temp->campo_mqtt) - 1);
    renkeer_temp->ciclo_leitura_ms = 5000;
}

static void montar_chave_dispositivo(uint8_t indice, char *destino, size_t tamanho)
{
    snprintf(destino, tamanho, "dispositivo_%u", (unsigned)indice);
}

static cJSON *barramento_para_json(const modbus_rs485_configuracao_t *config)
{
    cJSON *raiz = cJSON_CreateObject();

    cJSON_AddBoolToObject(raiz, "habilitado", config->habilitado);
    cJSON_AddNumberToObject(raiz, "baud_rate", config->baud_rate);
    cJSON_AddNumberToObject(raiz, "paridade", (double)config->paridade);
    cJSON_AddNumberToObject(raiz, "bits_dados", config->bits_dados);
    cJSON_AddNumberToObject(raiz, "bits_parada", config->bits_parada);
    cJSON_AddNumberToObject(raiz, "timeout_resposta_ms", config->timeout_resposta_ms);
    cJSON_AddNumberToObject(raiz, "intervalo_entre_requisicoes_ms", config->intervalo_entre_requisicoes_ms);
    cJSON_AddNumberToObject(raiz, "quantidade_tentativas", config->quantidade_tentativas);
    cJSON_AddNumberToObject(raiz, "limite_falhas_offline", config->limite_falhas_offline);
    cJSON_AddNumberToObject(raiz, "limite_sucessos_recuperacao", config->limite_sucessos_recuperacao);

    return raiz;
}

static void barramento_de_json(const cJSON *raiz, modbus_rs485_configuracao_t *destino)
{
    const cJSON *campo;

    if ((campo = cJSON_GetObjectItem(raiz, "habilitado")) != NULL) destino->habilitado = cJSON_IsTrue(campo);
    if ((campo = cJSON_GetObjectItem(raiz, "baud_rate")) != NULL) destino->baud_rate = (uint32_t)campo->valuedouble;
    if ((campo = cJSON_GetObjectItem(raiz, "paridade")) != NULL) destino->paridade = (uart_parity_t)campo->valueint;
    if ((campo = cJSON_GetObjectItem(raiz, "bits_dados")) != NULL) destino->bits_dados = (uint8_t)campo->valueint;
    if ((campo = cJSON_GetObjectItem(raiz, "bits_parada")) != NULL) destino->bits_parada = (uint8_t)campo->valueint;
    if ((campo = cJSON_GetObjectItem(raiz, "timeout_resposta_ms")) != NULL) destino->timeout_resposta_ms = (uint32_t)campo->valuedouble;
    if ((campo = cJSON_GetObjectItem(raiz, "intervalo_entre_requisicoes_ms")) != NULL) destino->intervalo_entre_requisicoes_ms = (uint32_t)campo->valuedouble;
    if ((campo = cJSON_GetObjectItem(raiz, "quantidade_tentativas")) != NULL) destino->quantidade_tentativas = (uint8_t)campo->valueint;
    if ((campo = cJSON_GetObjectItem(raiz, "limite_falhas_offline")) != NULL) destino->limite_falhas_offline = (uint32_t)campo->valuedouble;
    if ((campo = cJSON_GetObjectItem(raiz, "limite_sucessos_recuperacao")) != NULL) destino->limite_sucessos_recuperacao = (uint32_t)campo->valuedouble;
}

// Editado por Eraldo Bispo - 23/06/2026 - serializa "item" (ja preenchido pelo chamador) e grava
// na chave NVS indicada, com guarda de tamanho (MODBUS_RS485_JSON_TAM_MAX). "item" e sempre
// destruido aqui (cJSON_Delete), mesmo em erro - dono unico, chamador nao precisa se preocupar.
static esp_err_t gravar_json_em_nvs(nvs_handle_t nvs, const char *chave, cJSON *item)
{
    char *texto_json = cJSON_PrintUnformatted(item);
    cJSON_Delete(item);

    if (texto_json == NULL)
    {
        ESP_LOGE(TAG, "Erro ao serializar '%s' para JSON", chave);
        return ESP_FAIL;
    }

    if (strlen(texto_json) >= MODBUS_RS485_JSON_TAM_MAX)
    {
        ESP_LOGE(TAG, "JSON de '%s' maior que o limite (%d bytes)", chave, MODBUS_RS485_JSON_TAM_MAX);
        cJSON_free(texto_json);
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t ret = nvs_set_str(nvs, chave, texto_json);
    cJSON_free(texto_json);

    return ret;
}

// Editado por Eraldo Bispo - 23/06/2026 - antes a configuracao inteira (barramento + 4
// dispositivos x 4 canais) era gravada num unico JSON na chave "config", que passava do limite
// de tamanho (ver comentario de MODBUS_RS485_JSON_TAM_MAX acima) e travava o boot. Agora cada
// dispositivo tem sua propria chave NVS ("dispositivo_0".."dispositivo_3"), bem menor.
static esp_err_t gravar_em_nvs(const modbus_rs485_configuracao_t *config)
{
    nvs_handle_t nvs;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Erro ao abrir namespace NVS '%s'", NVS_NAMESPACE);
        return ret;
    }

    ret = gravar_json_em_nvs(nvs, NVS_CHAVE_BARRAMENTO, barramento_para_json(config));

    for (uint8_t i = 0; ret == ESP_OK && i < MODBUS_RS485_MAX_DISPOSITIVOS; i++)
    {
        char chave[20];
        montar_chave_dispositivo(i, chave, sizeof(chave));

        cJSON *item = cJSON_CreateObject();
        dispositivo_para_json(item, &config->dispositivos[i]);

        ret = gravar_json_em_nvs(nvs, chave, item);
    }

    if (ret == ESP_OK)
    {
        ret = nvs_commit(nvs);
    }

    nvs_close(nvs);

    return ret;
}

// Editado por Eraldo Bispo - 23/06/2026 - le e faz parse de uma chave JSON de NVS. Devolve
// ESP_ERR_NVS_NOT_FOUND se a chave nao existir (chamador decide o que fazer - ver ler_de_nvs).
static esp_err_t ler_json_de_nvs(nvs_handle_t nvs, const char *chave, cJSON **item_lido)
{
    *item_lido = NULL;

    size_t tamanho = 0;
    esp_err_t ret = nvs_get_str(nvs, chave, NULL, &tamanho);

    if (ret != ESP_OK)
    {
        return ret;
    }

    char *texto_json = malloc(tamanho);

    if (texto_json == NULL)
    {
        ESP_LOGE(TAG, "Sem memoria para ler '%s' (%u bytes)", chave, (unsigned)tamanho);
        return ESP_ERR_NO_MEM;
    }

    ret = nvs_get_str(nvs, chave, texto_json, &tamanho);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Erro ao ler '%s' de NVS: 0x%x", chave, ret);
        free(texto_json);
        return ret;
    }

    cJSON *item = cJSON_Parse(texto_json);
    free(texto_json);

    if (item == NULL)
    {
        ESP_LOGE(TAG, "JSON invalido salvo em '%s'", chave);
        return ESP_ERR_INVALID_STATE;
    }

    *item_lido = item;
    return ESP_OK;
}

static esp_err_t ler_de_nvs(modbus_rs485_configuracao_t *destino, bool *existia)
{
    *existia = false;
    memset(destino, 0, sizeof(*destino));

    nvs_handle_t nvs;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);

    if (ret != ESP_OK)
    {
        return ret;
    }

    cJSON *barramento = NULL;
    ret = ler_json_de_nvs(nvs, NVS_CHAVE_BARRAMENTO, &barramento);

    if (ret != ESP_OK)
    {
        nvs_close(nvs);
        return ret;
    }

    barramento_de_json(barramento, destino);
    cJSON_Delete(barramento);

    for (uint8_t i = 0; i < MODBUS_RS485_MAX_DISPOSITIVOS; i++)
    {
        char chave[20];
        montar_chave_dispositivo(i, chave, sizeof(chave));

        cJSON *dispositivo_json = NULL;

        // Editado por Eraldo Bispo - 23/06/2026 - se faltar a chave de um dispositivo (ex:
        // config gravada quando MODBUS_RS485_MAX_DISPOSITIVOS era menor), o dispositivo so fica
        // zerado (memset acima) em vez de invalidar toda a configuracao do barramento.
        if (ler_json_de_nvs(nvs, chave, &dispositivo_json) == ESP_OK)
        {
            dispositivo_de_json(dispositivo_json, &destino->dispositivos[i]);
            cJSON_Delete(dispositivo_json);
        }
    }

    nvs_close(nvs);

    *existia = true;
    return ESP_OK;
}

esp_err_t Modbus_RS485_Config_Carregar(modbus_rs485_configuracao_t *destino)
{
    if (destino == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    bool existia = false;
    esp_err_t ret = ler_de_nvs(destino, &existia);

    if (existia)
    {
        ESP_LOGI(TAG, "Configuracao Modbus RS485 carregada de NVS");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Configuracao Modbus RS485 nao encontrada em NVS (ret=0x%x), gravando padrao de fabrica", ret);
    carregar_padrao_fabrica(destino);

    return Modbus_RS485_Config_Salvar(destino);
}

esp_err_t Modbus_RS485_Config_Salvar(const modbus_rs485_configuracao_t *config)
{
    if (config == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    return gravar_em_nvs(config);
}

void Modbus_RS485_Config_FormatarParidade(const modbus_rs485_configuracao_t *config, char *destino, size_t tamanho_destino)
{
    if (destino == NULL || tamanho_destino == 0)
    {
        return;
    }

    char letra_paridade = 'N';

    if (config->paridade == UART_PARITY_EVEN)
    {
        letra_paridade = 'E';
    }
    else if (config->paridade == UART_PARITY_ODD)
    {
        letra_paridade = 'O';
    }

    snprintf(destino, tamanho_destino, "%u%c%u", config->bits_dados, letra_paridade, config->bits_parada);
}
