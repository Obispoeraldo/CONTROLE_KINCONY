/*
 * modbus_rs485_master.c
 *
 * Criado por Eraldo Bispo - 23/06/2026
 * Ver motivo e arquitetura completa em modbus_rs485_master.h
 */

#include "modbus_rs485_master.h"

#include <inttypes.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_log.h"
#include "esp_modbus_master.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "MODBUS_RS485_MASTER";

// Editado por Eraldo Bispo - 23/06/2026 - mesmos pinos reais da RS485 da KC868-A6 (corrigidos
// em 21/06/2026: GPIO27=TX, GPIO14=RX, sem DE/RE porque o transceptor MAX13487EESA da placa
// controla a direcao sozinho - ver historico em modbus_rtu_slave_esp.c).
#define MODBUS_RS485_UART_PORT          UART_NUM_2
#define MODBUS_RS485_TX_PIN             27
#define MODBUS_RS485_RX_PIN             14

#define MODBUS_RS485_TASK_STACK         4096
#define MODBUS_RS485_TASK_PRIORIDADE    (tskIDLE_PRIORITY + 1)

#define MODBUS_RS485_MUTEX_TIMEOUT_MS   200
#define MODBUS_RS485_CICLO_PADRAO_MS    2000
#define MODBUS_RS485_PAUSA_ENTRE_VOLTAS_MS 50

// Editado por Eraldo Bispo - 23/06/2026 - registradores suficientes pros tipos suportados
// (uint16/int16 = 1, uint32/int32/float32 = 2); folga proposital para nao truncar configuracoes
// um pouco maiores sem precisar de alocacao dinamica.
#define MODBUS_RS485_MAX_REGISTRADORES_POR_LEITURA 8

static modbus_rs485_configuracao_t configuracao_atual;
static SemaphoreHandle_t mutex_estado = NULL;
static void *master_handle = NULL;
static bool motor_ativo = false;

// Criado por Eraldo Bispo - 23/06/2026 - usado SO para pacing do scheduler (nao e estado
// "publico" do canal, por isso nao entra em modbus_rs485_config.h): registra o timestamp da
// ULTIMA TENTATIVA de leitura de cada canal, mesmo quando ela falha. Sem isso, um canal cujo
// dispositivo nao responde seria tentado de novo a cada volta do scheduler (a cada ~50ms) em
// vez de respeitar o ciclo_leitura_ms configurado.
static int64_t timestamp_ultima_tentativa_canal_ms[MODBUS_RS485_MAX_DISPOSITIVOS][MODBUS_RS485_MAX_CANAIS_POR_DISPOSITIVO];

// Criado por Eraldo Bispo - 23/06/2026 - tabela de descritores exigida pelo esp-modbus
// (mbc_master_start() falha com ESP_ERR_INVALID_ARG se estiver vazia). Nao usamos a API
// baseada em CID (mbc_master_get_parameter) - so mbc_master_send_request() abaixo, que nao
// depende dela - entao essa entrada e so um placeholder valido pra satisfazer a biblioteca.
static const mb_parameter_descriptor_t descritor_minimo[] = {
    { 0, "generico", "_", 1, MB_PARAM_HOLDING, 0, 1, 0, PARAM_TYPE_U16, 2, { { 0 } }, PAR_PERMS_READ }
};

static uart_word_length_t bits_dados_para_enum(uint8_t bits)
{
    switch (bits)
    {
        case 5: return UART_DATA_5_BITS;
        case 6: return UART_DATA_6_BITS;
        case 7: return UART_DATA_7_BITS;
        case 8:
        default: return UART_DATA_8_BITS;
    }
}

static uart_stop_bits_t bits_parada_para_enum(uint8_t bits)
{
    switch (bits)
    {
        case 2: return UART_STOP_BITS_2;
        case 1:
        default: return UART_STOP_BITS_1;
    }
}

static uint16_t trocar_bytes_registrador(uint16_t registrador)
{
    return (uint16_t)(((registrador & 0x00FF) << 8) | ((registrador & 0xFF00) >> 8));
}

// Editado por Eraldo Bispo - 23/06/2026 - converte os registradores brutos (ja em ordem host,
// o esp-modbus troca os bytes de rede pra host internamente) pro valor de engenharia, segundo
// tipo de dado + ordem de palavra/byte (so relevante pra 32 bits) + escala + deslocamento
// configurados no canal. Convencao ABCD/CDAB/BADC/DCBA igual a usada pelo proprio esp-modbus
// (PARAM_TYPE_U32_*, ver esp_modbus_master.h): A=byte mais significativo, D=menos significativo.
static float converter_valor(const modbus_canal_t *canal, const uint16_t *registradores)
{
    float valor_bruto = 0.0f;

    switch (canal->tipo_dado)
    {
        case MODBUS_TIPO_DADO_UINT16:
            valor_bruto = (float)registradores[0];
            break;

        case MODBUS_TIPO_DADO_INT16:
            valor_bruto = (float)(int16_t)registradores[0];
            break;

        case MODBUS_TIPO_DADO_UINT32:
        case MODBUS_TIPO_DADO_INT32:
        case MODBUS_TIPO_DADO_FLOAT32:
        default:
        {
            uint16_t palavra_alta;
            uint16_t palavra_baixa;

            switch (canal->ordem_palavra)
            {
                case MODBUS_ORDEM_PALAVRA_CDAB:
                    palavra_alta = registradores[1];
                    palavra_baixa = registradores[0];
                    break;

                case MODBUS_ORDEM_PALAVRA_BADC:
                    palavra_alta = trocar_bytes_registrador(registradores[0]);
                    palavra_baixa = trocar_bytes_registrador(registradores[1]);
                    break;

                case MODBUS_ORDEM_PALAVRA_DCBA:
                    palavra_alta = trocar_bytes_registrador(registradores[1]);
                    palavra_baixa = trocar_bytes_registrador(registradores[0]);
                    break;

                case MODBUS_ORDEM_PALAVRA_ABCD:
                default:
                    palavra_alta = registradores[0];
                    palavra_baixa = registradores[1];
                    break;
            }

            uint32_t valor_32_bits = ((uint32_t)palavra_alta << 16) | (uint32_t)palavra_baixa;

            if (canal->tipo_dado == MODBUS_TIPO_DADO_UINT32)
            {
                valor_bruto = (float)valor_32_bits;
            }
            else if (canal->tipo_dado == MODBUS_TIPO_DADO_INT32)
            {
                valor_bruto = (float)(int32_t)valor_32_bits;
            }
            else
            {
                float valor_float = 0.0f;
                memcpy(&valor_float, &valor_32_bits, sizeof(valor_float));
                valor_bruto = valor_float;
            }

            break;
        }
    }

    return (valor_bruto * canal->escala) + canal->deslocamento;
}

static esp_err_t iniciar_uart_e_master(void)
{
    mb_communication_info_t comm = {
        .ser_opts.port = MODBUS_RS485_UART_PORT,
        .ser_opts.mode = MB_RTU,
        .ser_opts.baudrate = configuracao_atual.baud_rate,
        .ser_opts.parity = configuracao_atual.paridade,
        .ser_opts.uid = 0,
        .ser_opts.response_tout_ms = configuracao_atual.timeout_resposta_ms,
        .ser_opts.data_bits = bits_dados_para_enum(configuracao_atual.bits_dados),
        .ser_opts.stop_bits = bits_parada_para_enum(configuracao_atual.bits_parada)
    };

    esp_err_t err = mbc_master_create_serial(&comm, &master_handle);

    if (err != ESP_OK || master_handle == NULL)
    {
        ESP_LOGE(TAG, "Erro mbc_master_create_serial: 0x%x", err);
        return err;
    }

    // Editado por Eraldo Bispo - 23/06/2026 - sem pino RTS/DE-RE (UART_PIN_NO_CHANGE): o
    // transceptor RS485 da KC868-A6 controla a direcao automaticamente.
    err = uart_set_pin(MODBUS_RS485_UART_PORT, MODBUS_RS485_TX_PIN, MODBUS_RS485_RX_PIN,
                        UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Erro uart_set_pin: 0x%x", err);
        return err;
    }

    err = uart_set_mode(MODBUS_RS485_UART_PORT, UART_MODE_UART);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Erro uart_set_mode: 0x%x", err);
        return err;
    }

    err = mbc_master_set_descriptor(master_handle, descritor_minimo,
                                     sizeof(descritor_minimo) / sizeof(descritor_minimo[0]));

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Erro mbc_master_set_descriptor: 0x%x", err);
        return err;
    }

    err = mbc_master_start(master_handle);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Erro mbc_master_start: 0x%x", err);
        return err;
    }

    char paridade_log[4];
    Modbus_RS485_Config_FormatarParidade(&configuracao_atual, paridade_log, sizeof(paridade_log));

    ESP_LOGI(TAG, "Motor Modbus RS485 master iniciado | UART=%d TX=%d RX=%d Baud=%" PRIu32 " %s",
             MODBUS_RS485_UART_PORT, MODBUS_RS485_TX_PIN, MODBUS_RS485_RX_PIN,
             configuracao_atual.baud_rate, paridade_log);

    return ESP_OK;
}

static void ler_canal(uint8_t indice_dispositivo, uint8_t indice_canal)
{
    modbus_dispositivo_t *dispositivo = &configuracao_atual.dispositivos[indice_dispositivo];
    modbus_canal_t *canal = &dispositivo->canais[indice_canal];

    timestamp_ultima_tentativa_canal_ms[indice_dispositivo][indice_canal] = esp_timer_get_time() / 1000;

    uint16_t quantidade = (canal->quantidade_registradores > 0) ? canal->quantidade_registradores : 1;

    if (quantidade > MODBUS_RS485_MAX_REGISTRADORES_POR_LEITURA)
    {
        ESP_LOGW(TAG, "Canal '%s': quantidade_registradores (%u) maior que o suportado (%u), truncando",
                 canal->nome, quantidade, MODBUS_RS485_MAX_REGISTRADORES_POR_LEITURA);
        quantidade = MODBUS_RS485_MAX_REGISTRADORES_POR_LEITURA;
    }

    uint16_t registradores_brutos[MODBUS_RS485_MAX_REGISTRADORES_POR_LEITURA] = { 0 };

    mb_param_request_t requisicao = {
        .slave_addr = dispositivo->endereco_escravo,
        .command = (uint8_t)MODBUS_FUNCAO_LEITURA_HOLDING,
        .reg_start = canal->endereco_inicial,
        .reg_size = quantidade
    };

    uint8_t tentativas = (configuracao_atual.quantidade_tentativas > 0) ? configuracao_atual.quantidade_tentativas : 1;
    esp_err_t status = ESP_FAIL;

    for (uint8_t tentativa = 0; tentativa < tentativas; tentativa++)
    {
        status = mbc_master_send_request(master_handle, &requisicao, (void *)registradores_brutos);

        if (status == ESP_OK)
        {
            break;
        }

        if (tentativa + 1 < tentativas)
        {
            vTaskDelay(pdMS_TO_TICKS(configuracao_atual.intervalo_entre_requisicoes_ms));
        }
    }

    int64_t agora_ms = esp_timer_get_time() / 1000;

    if (xSemaphoreTake(mutex_estado, pdMS_TO_TICKS(MODBUS_RS485_MUTEX_TIMEOUT_MS)) != pdTRUE)
    {
        ESP_LOGW(TAG, "Timeout ao obter mutex apos leitura do canal '%s' - valor descartado nesta volta", canal->nome);
        return;
    }

    dispositivo->timestamp_ultima_tentativa_ms = agora_ms;
    dispositivo->ultimo_erro = status;

    if (status == ESP_OK)
    {
        dispositivo->contador_sucesso++;
        dispositivo->contador_falha_consecutiva = 0;
        dispositivo->contador_sucesso_consecutivo++;
        dispositivo->timestamp_ultima_comunicacao_ms = agora_ms;

        float valor = converter_valor(canal, registradores_brutos);

        canal->ultimo_valor = valor;
        canal->ultimo_valor_valido = true;
        canal->timestamp_ultimo_valor_ms = agora_ms;

        if (!dispositivo->online && dispositivo->contador_sucesso_consecutivo >= configuracao_atual.limite_sucessos_recuperacao)
        {
            dispositivo->online = true;
            ESP_LOGI(TAG, "Dispositivo '%s' recuperado (online)", dispositivo->nome);
        }

        ESP_LOGI(TAG, "'%s' / '%s' = %.*f %s", dispositivo->nome, canal->nome,
                 canal->casas_decimais, valor, canal->unidade);
    }
    else
    {
        dispositivo->contador_falha_total++;
        dispositivo->contador_falha_consecutiva++;
        dispositivo->contador_sucesso_consecutivo = 0;

        if (status == ESP_ERR_TIMEOUT)
        {
            dispositivo->contador_timeout++;
        }
        else
        {
            dispositivo->contador_exception++;
        }

        if (dispositivo->online && dispositivo->contador_falha_consecutiva >= configuracao_atual.limite_falhas_offline)
        {
            dispositivo->online = false;

            for (uint8_t i = 0; i < MODBUS_RS485_MAX_CANAIS_POR_DISPOSITIVO; i++)
            {
                dispositivo->canais[i].ultimo_valor_valido = false;
            }

            ESP_LOGW(TAG, "Dispositivo '%s' OFFLINE apos %" PRIu32 " falhas consecutivas",
                     dispositivo->nome, dispositivo->contador_falha_consecutiva);
        }

        ESP_LOGW(TAG, "Falha ao ler canal '%s' do dispositivo '%s': 0x%x (%s)",
                 canal->nome, dispositivo->nome, status, esp_err_to_name(status));
    }

    xSemaphoreGive(mutex_estado);
}

static void modbus_rs485_master_task(void *parametros)
{
    (void)parametros;

    while (1)
    {
        for (uint8_t indice_dispositivo = 0; indice_dispositivo < MODBUS_RS485_MAX_DISPOSITIVOS; indice_dispositivo++)
        {
            modbus_dispositivo_t *dispositivo = &configuracao_atual.dispositivos[indice_dispositivo];

            if (!dispositivo->habilitado)
            {
                continue;
            }

            for (uint8_t indice_canal = 0; indice_canal < MODBUS_RS485_MAX_CANAIS_POR_DISPOSITIVO; indice_canal++)
            {
                modbus_canal_t *canal = &dispositivo->canais[indice_canal];

                if (!canal->habilitado)
                {
                    continue;
                }

                uint32_t ciclo = (canal->ciclo_leitura_ms > 0) ? canal->ciclo_leitura_ms : MODBUS_RS485_CICLO_PADRAO_MS;
                int64_t agora_ms = esp_timer_get_time() / 1000;
                int64_t ultima_tentativa = timestamp_ultima_tentativa_canal_ms[indice_dispositivo][indice_canal];

                if (ultima_tentativa != 0 && (agora_ms - ultima_tentativa) < (int64_t)ciclo)
                {
                    continue;
                }

                ler_canal(indice_dispositivo, indice_canal);

                vTaskDelay(pdMS_TO_TICKS(configuracao_atual.intervalo_entre_requisicoes_ms));
            }
        }

        vTaskDelay(pdMS_TO_TICKS(MODBUS_RS485_PAUSA_ENTRE_VOLTAS_MS));
    }
}

esp_err_t Modbus_RS485_Iniciar(void)
{
    esp_err_t ret = Modbus_RS485_Config_Carregar(&configuracao_atual);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Erro ao carregar configuracao do Modbus RS485: 0x%x", ret);
        return ret;
    }

    mutex_estado = xSemaphoreCreateMutex();

    if (mutex_estado == NULL)
    {
        ESP_LOGE(TAG, "Erro ao criar mutex de estado do Modbus RS485");
        return ESP_ERR_NO_MEM;
    }

    if (!configuracao_atual.habilitado)
    {
        ESP_LOGW(TAG, "Modbus RS485 desabilitado na configuracao - motor nao foi iniciado");
        return ESP_OK;
    }

    ret = iniciar_uart_e_master();

    if (ret != ESP_OK)
    {
        return ret;
    }

    BaseType_t criada = xTaskCreate(modbus_rs485_master_task, "modbus_rs485_master",
                                     MODBUS_RS485_TASK_STACK, NULL, MODBUS_RS485_TASK_PRIORIDADE, NULL);

    if (criada != pdPASS)
    {
        ESP_LOGE(TAG, "Erro ao criar task do Modbus RS485 master");
        return ESP_FAIL;
    }

    motor_ativo = true;

    return ESP_OK;
}

bool Modbus_RS485_EstaHabilitado(void)
{
    return motor_ativo;
}

esp_err_t Modbus_RS485_ObterSnapshot(modbus_rs485_configuracao_t *destino)
{
    if (destino == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (mutex_estado == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(mutex_estado, pdMS_TO_TICKS(MODBUS_RS485_MUTEX_TIMEOUT_MS)) != pdTRUE)
    {
        ESP_LOGW(TAG, "Timeout ao obter mutex para snapshot da configuracao");
        return ESP_ERR_TIMEOUT;
    }

    memcpy(destino, &configuracao_atual, sizeof(modbus_rs485_configuracao_t));

    xSemaphoreGive(mutex_estado);

    return ESP_OK;
}

