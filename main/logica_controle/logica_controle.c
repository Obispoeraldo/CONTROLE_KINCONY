#include "logica_controle.h"

#include "entradas_kincony.h"
#include "saidas_digitais_kincony.h"

#include "esp_log.h"

#define TAG "LOGICA_CTRL"

logica_monitor_grupo_t logica_grupos[LOGICA_CONTROLE_NUM_GRUPOS];

bool logica_modo_remoto = false;
bool logica_falha_geral = false;
uint8_t logica_mascara_grupos_ligados = 0;
uint8_t logica_mascara_falhas = 0;
// Criado por Eraldo Bispo - 18/06/2026 22:17 - estado atual do alarme geral (saida 06)
bool logica_alarme_ativo = false;

static bool remoto_anterior = false;
static TickType_t tick_ultima_partida = 0;

static const entrada_kincony_t entradas_feedback[LOGICA_CONTROLE_NUM_GRUPOS] = {
    GRUPO_MOTOR1,
    GRUPO_MOTOR2,
    GRUPO_MOTOR3,
    GRUPO_MOTOR4,
    GRUPO_MOTOR5
};

static const saida_kincony_t saidas_grupos[LOGICA_CONTROLE_NUM_GRUPOS] = {
    SAIDA_1,
    SAIDA_2,
    SAIDA_3,
    SAIDA_4,
    SAIDA_5
};

static bool ler_modo_remoto(void)
{
#if LOGICA_USAR_ENTRADA_6_COMO_REMOTO
    return Entradas_Kincony_Get(GRUPO_MOTOR6) ? true : false;
#else
    return Entradas_Kincony_Get(CHAVE_REMOTO) ? true : false;
#endif
}

static bool indice_valido(logica_grupo_t grupo)
{
    return (grupo >= LOGICA_GRUPO_1 && grupo < LOGICA_GRUPO_QTD);
}

static bool tempo_passou(TickType_t inicio, uint32_t tempo_ms)
{
    TickType_t agora = xTaskGetTickCount();
    return ((agora - inicio) >= pdMS_TO_TICKS(tempo_ms));
}

static bool pode_partir_novo_grupo(void)
{
    return tempo_passou(tick_ultima_partida, LOGICA_INTERVALO_ENTRE_PARTIDAS_MS);
}

static void atualizar_mascaras(void)
{
    logica_mascara_grupos_ligados = 0;
    logica_mascara_falhas = 0;
    logica_falha_geral = false;

    for (uint8_t i = 0; i < LOGICA_CONTROLE_NUM_GRUPOS; i++)
    {
        if (logica_grupos[i].feedback || logica_grupos[i].comando_aplicado)
        {
            logica_mascara_grupos_ligados |= (1 << i);
        }

        if (logica_grupos[i].falha != LOGICA_FALHA_NENHUMA ||
            logica_grupos[i].estado == LOGICA_ESTADO_FALHA)
        {
            logica_mascara_falhas |= (1 << i);
            logica_falha_geral = true;
        }
    }

    if (!Entradas_Kincony_IsOnline() || !Saidas_Kincony_IsOnline())
    {
        logica_falha_geral = true;
    }
}

static void setar_falha(uint8_t i, logica_tipo_falha_t falha)
{
    logica_grupos[i].falha = falha;
    logica_grupos[i].estado = LOGICA_ESTADO_FALHA;
    logica_grupos[i].tick_estado = xTaskGetTickCount();

    /* Em falha, derruba a saida do grupo para nao ficar tentando partir. */
    Saidas_Kincony_Desligar(saidas_grupos[i]);
    logica_grupos[i].comando_aplicado = false;

    ESP_LOGW(TAG, "Grupo %d em FALHA: %s", i + 1, Logica_Controle_FalhaToString(falha));
}

static void aplicar_saida(uint8_t i, bool ligar)
{
    esp_err_t ret;

    if (ligar)
    {
        ret = Saidas_Kincony_Ligar(saidas_grupos[i]);
    }
    else
    {
        ret = Saidas_Kincony_Desligar(saidas_grupos[i]);
    }

    if (ret != ESP_OK)
    {
        setar_falha(i, LOGICA_FALHA_SAIDAS_OFFLINE);
        return;
    }

    logica_grupos[i].comando_aplicado = ligar;
    logica_grupos[i].tick_comando = xTaskGetTickCount();
}

static void processar_grupo(uint8_t i)
{
    logica_monitor_grupo_t *g = &logica_grupos[i];
    TickType_t agora = xTaskGetTickCount();

    g->feedback = Entradas_Kincony_Get(entradas_feedback[i]) ? true : false;

    if (!Entradas_Kincony_IsOnline())
    {
        setar_falha(i, LOGICA_FALHA_ENTRADAS_OFFLINE);
        return;
    }

    if (g->estado == LOGICA_ESTADO_FALHA)
    {
        return;
    }

    switch (g->estado)
    {
        case LOGICA_ESTADO_DESLIGADO:
        {
            g->comando_aplicado = Saidas_Kincony_Get(saidas_grupos[i]);

            if (logica_modo_remoto && g->comando_desejado)
            {
                if (pode_partir_novo_grupo())
                {
                    aplicar_saida(i, true);
                    tick_ultima_partida = agora;

                    g->estado = LOGICA_ESTADO_AGUARDANDO_PARTIDA;
                    g->tick_estado = agora;
                    g->aguardando_intervalo = false;

                    ESP_LOGI(TAG, "Grupo %d: partida solicitada", i + 1);
                }
                else
                {
                    g->aguardando_intervalo = true;
                }
            }
            break;
        }

        case LOGICA_ESTADO_AGUARDANDO_PARTIDA:
        {
            if (!g->comando_desejado)
            {
                aplicar_saida(i, false);
                g->estado = LOGICA_ESTADO_AGUARDANDO_PARADA;
                g->tick_estado = agora;
                break;
            }

            if (g->feedback)
            {
                g->estado = LOGICA_ESTADO_LIGADO_OK;
                g->tick_estado = agora;
                g->falha = LOGICA_FALHA_NENHUMA;
                ESP_LOGI(TAG, "Grupo %d: feedback confirmado", i + 1);
            }
            else if (tempo_passou(g->tick_estado, LOGICA_TIMEOUT_PARTIDA_MS))
            {
                setar_falha(i, LOGICA_FALHA_TIMEOUT_PARTIDA);
            }
            break;
        }

        case LOGICA_ESTADO_LIGADO_OK:
        {
            if (!g->comando_desejado || !logica_modo_remoto)
            {
                aplicar_saida(i, false);
                g->estado = LOGICA_ESTADO_AGUARDANDO_PARADA;
                g->tick_estado = agora;
                ESP_LOGI(TAG, "Grupo %d: parada solicitada", i + 1);
            }
            else if (!g->feedback)
            {
                setar_falha(i, LOGICA_FALHA_PERDA_FEEDBACK);
            }
            break;
        }

        case LOGICA_ESTADO_AGUARDANDO_PARADA:
        {
            if (!g->feedback)
            {
                g->estado = LOGICA_ESTADO_DESLIGADO;
                g->tick_estado = agora;
                g->falha = LOGICA_FALHA_NENHUMA;
                g->comando_aplicado = false;
                ESP_LOGI(TAG, "Grupo %d: parada confirmada", i + 1);
            }
            else if (tempo_passou(g->tick_estado, LOGICA_TIMEOUT_PARADA_MS))
            {
                setar_falha(i, LOGICA_FALHA_TIMEOUT_PARADA);
            }
            break;
        }

        case LOGICA_ESTADO_FALHA:
        default:
            break;
    }
}

esp_err_t Logica_Controle_Iniciar(void)
{
    for (uint8_t i = 0; i < LOGICA_CONTROLE_NUM_GRUPOS; i++)
    {
        logica_grupos[i].estado = LOGICA_ESTADO_DESLIGADO;
        logica_grupos[i].falha = LOGICA_FALHA_NENHUMA;
        logica_grupos[i].comando_desejado = false;
        logica_grupos[i].comando_aplicado = false;
        logica_grupos[i].feedback = false;
        logica_grupos[i].aguardando_intervalo = false;
        logica_grupos[i].tick_estado = xTaskGetTickCount();
        logica_grupos[i].tick_comando = xTaskGetTickCount();
    }

    logica_modo_remoto = ler_modo_remoto();
    remoto_anterior = logica_modo_remoto;
    tick_ultima_partida = 0;

    atualizar_mascaras();

    ESP_LOGI(TAG, "Controle iniciado | modo=%s", logica_modo_remoto ? "REMOTO" : "MONITORAMENTO");
    return ESP_OK;
}

void Logica_Controle_Processar(void)
{
    logica_modo_remoto = ler_modo_remoto();

    if (logica_modo_remoto != remoto_anterior)
    {
        ESP_LOGI(TAG, "Modo alterado para: %s", logica_modo_remoto ? "REMOTO" : "MONITORAMENTO");

        if (!logica_modo_remoto)
        {
            for (uint8_t i = 0; i < LOGICA_CONTROLE_NUM_GRUPOS; i++)
            {
                logica_grupos[i].comando_desejado = false;
                logica_grupos[i].aguardando_intervalo = false;
            }

            // Editado por Eraldo Bispo - 18/06/2026 22:17 - troca de REMOTO para LOCAL e uma acao
            // normal do operador (chave fisica), nao uma falha: os grupos vao para AGUARDANDO_PARADA
            // (estado normal), nunca LOGICA_ESTADO_FALHA, entao isso nao ativa o alarme geral
            // (logica_falha_geral so e setado por timeout/perda de feedback/entradas-saidas offline,
            // ver atualizar_mascaras() e Logica_Controle_AtualizarAlarme()).
#if LOGICA_DESLIGAR_AO_SAIR_REMOTO
            for (uint8_t i = 0; i < LOGICA_CONTROLE_NUM_GRUPOS; i++)
            {
                aplicar_saida(i, false);
                logica_grupos[i].estado = LOGICA_ESTADO_AGUARDANDO_PARADA;
                logica_grupos[i].tick_estado = xTaskGetTickCount();
            }
#endif
        }

        remoto_anterior = logica_modo_remoto;
    }

    for (uint8_t i = 0; i < LOGICA_CONTROLE_NUM_GRUPOS; i++)
    {
        processar_grupo(i);
    }

    atualizar_mascaras();
}

esp_err_t Logica_Controle_SetComandoGrupo(logica_grupo_t grupo, bool ligar)
{
    if (!indice_valido(grupo))
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (!logica_modo_remoto)
    {
        logica_grupos[grupo].comando_desejado = false;
        return ESP_ERR_INVALID_STATE;
    }

    logica_grupos[grupo].comando_desejado = ligar;
    return ESP_OK;
}

esp_err_t Logica_Controle_AlternarGrupo(logica_grupo_t grupo)
{
    if (!indice_valido(grupo))
    {
        return ESP_ERR_INVALID_ARG;
    }

    return Logica_Controle_SetComandoGrupo(grupo, !logica_grupos[grupo].comando_desejado);
}

void Logica_Controle_DesligarTodos(void)
{
    for (uint8_t i = 0; i < LOGICA_CONTROLE_NUM_GRUPOS; i++)
    {
        logica_grupos[i].comando_desejado = false;
        logica_grupos[i].aguardando_intervalo = false;
        aplicar_saida(i, false);
    }
}

void Logica_Controle_ResetarFalhas(void)
{
    for (uint8_t i = 0; i < LOGICA_CONTROLE_NUM_GRUPOS; i++)
    {
        Logica_Controle_ResetarFalhaGrupo((logica_grupo_t)i);
    }
}

void Logica_Controle_ResetarFalhaGrupo(logica_grupo_t grupo)
{
    if (!indice_valido(grupo))
    {
        return;
    }

    logica_monitor_grupo_t *g = &logica_grupos[grupo];

    g->falha = LOGICA_FALHA_NENHUMA;
    g->aguardando_intervalo = false;
    g->tick_estado = xTaskGetTickCount();

    if (g->comando_desejado && logica_modo_remoto)
    {
        g->estado = LOGICA_ESTADO_DESLIGADO;
    }
    else if (g->feedback)
    {
        g->estado = LOGICA_ESTADO_AGUARDANDO_PARADA;
    }
    else
    {
        g->estado = LOGICA_ESTADO_DESLIGADO;
    }
}

bool Logica_Controle_IsRemoto(void)
{
    return logica_modo_remoto;
}

bool Logica_Controle_IsFalhaGeral(void)
{
    return logica_falha_geral;
}

bool Logica_Controle_GetComandoGrupo(logica_grupo_t grupo)
{
    if (!indice_valido(grupo)) return false;
    return logica_grupos[grupo].comando_desejado;
}

bool Logica_Controle_GetFeedbackGrupo(logica_grupo_t grupo)
{
    if (!indice_valido(grupo)) return false;
    return logica_grupos[grupo].feedback;
}

logica_estado_grupo_t Logica_Controle_GetEstadoGrupo(logica_grupo_t grupo)
{
    if (!indice_valido(grupo)) return LOGICA_ESTADO_DESLIGADO;
    return logica_grupos[grupo].estado;
}

logica_tipo_falha_t Logica_Controle_GetFalhaGrupo(logica_grupo_t grupo)
{
    if (!indice_valido(grupo)) return LOGICA_FALHA_NENHUMA;
    return logica_grupos[grupo].falha;
}

uint8_t Logica_Controle_GetMascaraLigados(void)
{
    return logica_mascara_grupos_ligados;
}

uint8_t Logica_Controle_GetMascaraFalhas(void)
{
    return logica_mascara_falhas;
}

const char *Logica_Controle_EstadoToString(logica_estado_grupo_t estado)
{
    switch (estado)
    {
        case LOGICA_ESTADO_DESLIGADO: return "DESLIGADO";
        case LOGICA_ESTADO_AGUARDANDO_PARTIDA: return "AGUARDANDO_PARTIDA";
        case LOGICA_ESTADO_LIGADO_OK: return "LIGADO_OK";
        case LOGICA_ESTADO_AGUARDANDO_PARADA: return "AGUARDANDO_PARADA";
        case LOGICA_ESTADO_FALHA: return "FALHA";
        default: return "DESCONHECIDO";
    }
}

const char *Logica_Controle_FalhaToString(logica_tipo_falha_t falha)
{
    switch (falha)
    {
        case LOGICA_FALHA_NENHUMA: return "NENHUMA";
        case LOGICA_FALHA_TIMEOUT_PARTIDA: return "TIMEOUT_PARTIDA";
        case LOGICA_FALHA_TIMEOUT_PARADA: return "TIMEOUT_PARADA";
        case LOGICA_FALHA_PERDA_FEEDBACK: return "PERDA_FEEDBACK";
        case LOGICA_FALHA_SAIDAS_OFFLINE: return "SAIDAS_OFFLINE";
        case LOGICA_FALHA_ENTRADAS_OFFLINE: return "ENTRADAS_OFFLINE";
        default: return "DESCONHECIDA";
    }
}

// Criado por Eraldo Bispo - 18/06/2026 22:17 - liga a saida 06 (alarme do controlador) sempre que
// houver falha de algum grupo (timeout de partida/parada, perda de feedback, entradas/saidas
// offline - tudo isso ja cai em logica_falha_geral via atualizar_mascaras()) OU perda de conexao
// com o broker MQTT OU perda do WiFi. Troca de modo REMOTO/LOCAL nao entra aqui propositalmente
// (ver comentario em Logica_Controle_Processar()), pois e operacao normal, nao falha.
// So escreve na saida quando o estado do alarme muda, para nao floodar o I2C a cada volta do loop.
void Logica_Controle_AtualizarAlarme(bool mqtt_conectado, bool wifi_conectado)
{
    bool alarme = logica_falha_geral || !mqtt_conectado || !wifi_conectado;

    if (alarme == logica_alarme_ativo)
    {
        return;
    }

    logica_alarme_ativo = alarme;

    if (alarme)
    {
        Saidas_Kincony_Ligar(SAIDA_6);
        ESP_LOGW(TAG, "Alarme ATIVADO (saida 06) | falha_geral=%d mqtt=%d wifi=%d",
                 logica_falha_geral, mqtt_conectado, wifi_conectado);
    }
    else
    {
        Saidas_Kincony_Desligar(SAIDA_6);
        ESP_LOGI(TAG, "Alarme DESATIVADO (saida 06)");
    }
}

bool Logica_Controle_IsAlarmeAtivo(void)
{
    return logica_alarme_ativo;
}
