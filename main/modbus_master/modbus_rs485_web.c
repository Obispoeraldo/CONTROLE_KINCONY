/*
 * modbus_rs485_web.c
 *
 * Criado por Eraldo Bispo - 23/06/2026
 * Ver contrato das paginas e motivo das decisoes em modbus_rs485_web.h
 */

#include "modbus_rs485_web.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_system.h"
#include "driver/uart.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "config_server_kincony.h"
#include "modbus_rs485_master.h"

static const char *TAG = "MODBUS_RS485_WEB";

static esp_err_t redirecionar(httpd_req_t *req, const char *destino)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", destino);
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t enviar_pagina_reiniciando(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req,
        "<!DOCTYPE html><html><head><meta charset='utf-8'></head>"
        "<body style='font-family:sans-serif;background:#0e2236;color:#fff;"
        "display:flex;justify-content:center;align-items:center;min-height:100vh'>"
        "<h3>Configura\xC3\xA7\xC3\xA3o salva. Reiniciando o Kincony...</h3>"
        "</body></html>", HTTPD_RESP_USE_STRLEN);

    ESP_LOGW(TAG, "Configuracao do Modbus RS485 alterada via painel web. Reiniciando em 2 segundos...");

    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();

    return ESP_OK;
}

static bool obter_indice_da_query(httpd_req_t *req, uint8_t *indice)
{
    char query[16];

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK)
    {
        return false;
    }

    char valor[4] = {0};

    if (httpd_query_key_value(query, "indice", valor, sizeof(valor)) != ESP_OK)
    {
        return false;
    }

    int numero = atoi(valor);

    if (numero < 0 || numero >= MODBUS_RS485_MAX_DISPOSITIVOS)
    {
        return false;
    }

    *indice = (uint8_t)numero;
    return true;
}

static esp_err_t handler_get_rs485(httpd_req_t *req)
{
    if (!Config_Server_Kincony_SessaoValida(req))
    {
        return redirecionar(req, "/login");
    }

    modbus_rs485_configuracao_t config;

    if (Modbus_RS485_ObterSnapshot(&config) != ESP_OK)
    {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    char letra_paridade = 'N';

    if (config.paridade == UART_PARITY_EVEN)
    {
        letra_paridade = 'E';
    }
    else if (config.paridade == UART_PARITY_ODD)
    {
        letra_paridade = 'O';
    }

    char linhas_dispositivos[1500];
    int posicao = 0;

    for (uint8_t i = 0; i < MODBUS_RS485_MAX_DISPOSITIVOS; i++)
    {
        const modbus_dispositivo_t *dispositivo = &config.dispositivos[i];
        // Editado por Eraldo Bispo - 24/06/2026 - slot sem nome E desabilitado = nunca foi
        // configurado ainda - chamamos de "+ Adicionar dispositivo" em vez de "Editar" pra deixar
        // claro que dá pra usar esse slot pra um dispositivo novo (o limite continua sendo
        // MODBUS_RS485_MAX_DISPOSITIVOS - nao tem como adicionar alem dos slots existentes).
        bool slot_vazio = (strlen(dispositivo->nome) == 0 && !dispositivo->habilitado);
        const char *nome = slot_vazio ? "(slot vazio)" : dispositivo->nome;
        const char *status = !dispositivo->habilitado ? "desabilitado" : (dispositivo->online ? "online" : "offline");
        const char *rotulo_link = slot_vazio ? "+ Adicionar dispositivo" : "Editar";

        posicao += snprintf(linhas_dispositivos + posicao, sizeof(linhas_dispositivos) - posicao,
            "<div class='linha-dispositivo'><b>%s</b><br>"
            "<span class='tag'>%s</span> &middot; endere\xC3\xA7o escravo %u &middot; mqtt '%s'"
            "<br><a href='/rs485/dispositivo?indice=%u'>%s</a></div>",
            nome, status, dispositivo->endereco_escravo, dispositivo->identificador_mqtt, (unsigned)i, rotulo_link);
    }

    // Editado por Eraldo Bispo - 28/06/2026 - buffer aumentado de 3200 para 6000: o CSS comum
    // (ESTILO_AQUAPULSE, ~1,1KB) + o HTML estatico da pagina (~1,9KB) + a lista de dispositivos
    // (linhas_dispositivos, ate 1,5KB) somam mais de 4,5KB - com 3200 bytes o snprintf truncava o
    // FINAL da string sem nenhum aviso (snprintf so corta, nao retorna erro), cortando justamente
    // o "Voltar" do rodape e o fechamento das tags, que ficam no final do formato.
    char pagina[6000];

    snprintf(pagina, sizeof(pagina),
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width, initial-scale=1'>"
        "<title>AquaPulse - RS485</title>%s"
        "<style>.linha-dispositivo{background:#fafbfc;border:1px solid #e3e6ec;border-radius:10px;"
        "padding:10px 14px;margin-bottom:10px;font-size:13px;color:#142a4d}"
        ".linha-dispositivo a{color:#0e93a6;text-decoration:none;font-weight:600}"
        ".tag{color:#5a6376}</style>"
        "</head>"
        "<body><div class='card' style='width:380px'>"
        "<img class='logo' src='/logo.jpg'>"
        "<h2>RS485 / Modbus RTU</h2>"
        "<p class='sub'>Configura\xC3\xA7\xC3\xA3o do barramento</p>"
        "<form method='POST' action='/rs485'>"
        "<label>Habilitado</label>"
        "<input type='checkbox' name='habilitado' value='1' %s style='width:auto;margin-bottom:16px'>"
        "<label>Baud rate</label>"
        "<input name='baud_rate' value='%lu' maxlength='7'>"
        "<label>Paridade</label>"
        "<select name='paridade'>"
        "<option value='N' %s>Nenhuma (N)</option>"
        "<option value='E' %s>Par (E)</option>"
        "<option value='O' %s>\xC3\x8dmpar (O)</option>"
        "</select>"
        "<label>Bits de dados</label>"
        "<input name='bits_dados' value='%u' maxlength='1'>"
        "<label>Bits de parada</label>"
        "<input name='bits_parada' value='%u' maxlength='1'>"
        "<label>Timeout de resposta (ms)</label>"
        "<input name='timeout_resposta_ms' value='%lu' maxlength='6'>"
        "<label>Intervalo entre requisi\xC3\xA7\xC3\xB5""es (ms)</label>"
        "<input name='intervalo_entre_requisicoes_ms' value='%lu' maxlength='6'>"
        "<label>Tentativas</label>"
        "<input name='quantidade_tentativas' value='%u' maxlength='2'>"
        "<label>Limite de falhas para offline</label>"
        "<input name='limite_falhas_offline' value='%lu' maxlength='4'>"
        "<label>Limite de sucessos para recupera\xC3\xA7\xC3\xA3o</label>"
        "<input name='limite_sucessos_recuperacao' value='%lu' maxlength='4'>"
        "<button type='submit'>Salvar e reiniciar</button>"
        "</form>"
        "<h3 style='color:#142a4d;font-size:15px;margin:22px 0 10px'>Dispositivos</h3>"
        "%s"
        "<div class='rodape'><a href='/configuracoes'>Voltar</a></div>"
        "</div></body></html>",
        Config_Server_Kincony_ObterEstiloComum(),
        config.habilitado ? "checked" : "",
        (unsigned long)config.baud_rate,
        letra_paridade == 'N' ? "selected" : "",
        letra_paridade == 'E' ? "selected" : "",
        letra_paridade == 'O' ? "selected" : "",
        config.bits_dados,
        config.bits_parada,
        (unsigned long)config.timeout_resposta_ms,
        (unsigned long)config.intervalo_entre_requisicoes_ms,
        config.quantidade_tentativas,
        (unsigned long)config.limite_falhas_offline,
        (unsigned long)config.limite_sucessos_recuperacao,
        linhas_dispositivos);

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, pagina, HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}

static esp_err_t handler_post_rs485(httpd_req_t *req)
{
    if (!Config_Server_Kincony_SessaoValida(req))
    {
        return redirecionar(req, "/login");
    }

    char corpo[500] = {0};
    int recebido = httpd_req_recv(req, corpo, sizeof(corpo) - 1);

    if (recebido <= 0)
    {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    corpo[recebido] = '\0';

    modbus_rs485_configuracao_t config;

    if (Modbus_RS485_Config_Carregar(&config) != ESP_OK)
    {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    char bruto[16];

    config.habilitado = (httpd_query_key_value(corpo, "habilitado", bruto, sizeof(bruto)) == ESP_OK);

    if (httpd_query_key_value(corpo, "baud_rate", bruto, sizeof(bruto)) == ESP_OK)
    {
        config.baud_rate = (uint32_t)atol(bruto);
    }

    if (httpd_query_key_value(corpo, "paridade", bruto, sizeof(bruto)) == ESP_OK)
    {
        if (bruto[0] == 'E')
        {
            config.paridade = UART_PARITY_EVEN;
        }
        else if (bruto[0] == 'O')
        {
            config.paridade = UART_PARITY_ODD;
        }
        else
        {
            config.paridade = UART_PARITY_DISABLE;
        }
    }

    if (httpd_query_key_value(corpo, "bits_dados", bruto, sizeof(bruto)) == ESP_OK)
    {
        config.bits_dados = (uint8_t)atoi(bruto);
    }

    if (httpd_query_key_value(corpo, "bits_parada", bruto, sizeof(bruto)) == ESP_OK)
    {
        config.bits_parada = (uint8_t)atoi(bruto);
    }

    if (httpd_query_key_value(corpo, "timeout_resposta_ms", bruto, sizeof(bruto)) == ESP_OK)
    {
        config.timeout_resposta_ms = (uint32_t)atol(bruto);
    }

    if (httpd_query_key_value(corpo, "intervalo_entre_requisicoes_ms", bruto, sizeof(bruto)) == ESP_OK)
    {
        config.intervalo_entre_requisicoes_ms = (uint32_t)atol(bruto);
    }

    if (httpd_query_key_value(corpo, "quantidade_tentativas", bruto, sizeof(bruto)) == ESP_OK)
    {
        config.quantidade_tentativas = (uint8_t)atoi(bruto);
    }

    if (httpd_query_key_value(corpo, "limite_falhas_offline", bruto, sizeof(bruto)) == ESP_OK)
    {
        config.limite_falhas_offline = (uint32_t)atol(bruto);
    }

    if (httpd_query_key_value(corpo, "limite_sucessos_recuperacao", bruto, sizeof(bruto)) == ESP_OK)
    {
        config.limite_sucessos_recuperacao = (uint32_t)atol(bruto);
    }

    esp_err_t ret = Modbus_RS485_Config_Salvar(&config);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Erro ao salvar configuracao do barramento Modbus RS485: 0x%x", ret);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    return enviar_pagina_reiniciando(req);
}

// Editado por Eraldo Bispo - 23/06/2026 - monta o fieldset HTML de UM canal num buffer
// reutilizado pelo chamador (ver handler_get_rs485_dispositivo) - a pagina do dispositivo e
// grande (4 canais x ~13 campos), por isso e enviada em pedacos (httpd_resp_send_chunk) em vez
// de um unico buffer gigante na pilha (ja causou stack overflow nesse servidor antes - ver
// comentario em Config_Server_Kincony_IniciarHttp()).
static void montar_fieldset_canal(char *destino, size_t tamanho, uint8_t indice_canal, const modbus_canal_t *canal)
{
    snprintf(destino, tamanho,
        "<fieldset style='border:1px solid #e3e6ec;border-radius:10px;padding:12px 14px;margin-bottom:14px'>"
        "<legend style='color:#142a4d;font-weight:600;font-size:13px'>Canal %u &middot; Leitura (FC03)</legend>"
        "<label>Habilitado</label>"
        "<input type='checkbox' name='c%u_habilitado' value='1' %s style='width:auto;margin-bottom:12px'>"
        "<label>Nome</label>"
        "<input name='c%u_nome' value='%s' maxlength='31'>"
        "<label>Endere\xC3\xA7o inicial (decimal)</label>"
        "<input name='c%u_endereco_inicial' value='%u' maxlength='5'>"
        "<label>Quantidade de registradores</label>"
        "<input name='c%u_quantidade_registradores' value='%u' maxlength='2'>"
        "<label>Tipo de dado</label>"
        "<select name='c%u_tipo_dado'>"
        "<option value='0' %s>uint16 (1 registrador)</option>"
        "<option value='1' %s>int16 (1 registrador)</option>"
        "<option value='2' %s>uint32 (2 registradores)</option>"
        "<option value='3' %s>int32 (2 registradores)</option>"
        "<option value='4' %s>float32 (2 registradores)</option>"
        "</select>"
        "<label>Ordem de palavra (32 bits)</label>"
        "<select name='c%u_ordem_palavra'>"
        "<option value='0' %s>ABCD (padr\xC3\xA3o)</option>"
        "<option value='1' %s>CDAB</option>"
        "<option value='2' %s>BADC</option>"
        "<option value='3' %s>DCBA</option>"
        "</select>"
        "<label>Escala</label>"
        "<input name='c%u_escala' value='%g' maxlength='12'>"
        "<label>Deslocamento</label>"
        "<input name='c%u_deslocamento' value='%g' maxlength='12'>"
        "<label>Casas decimais</label>"
        "<input name='c%u_casas_decimais' value='%u' maxlength='1'>"
        "<label>Unidade</label>"
        "<input name='c%u_unidade' value='%s' maxlength='7'>"
        "<label>Limite m\xC3\xADnimo habilitado</label>"
        "<input type='checkbox' name='c%u_limite_minimo_habilitado' value='1' %s style='width:auto;margin-bottom:12px'>"
        "<label>Limite m\xC3\xADnimo</label>"
        "<input name='c%u_limite_minimo' value='%g' maxlength='12'>"
        "<label>Limite m\xC3\xA1ximo habilitado</label>"
        "<input type='checkbox' name='c%u_limite_maximo_habilitado' value='1' %s style='width:auto;margin-bottom:12px'>"
        "<label>Limite m\xC3\xA1ximo</label>"
        "<input name='c%u_limite_maximo' value='%g' maxlength='12'>"
        "<label>Campo MQTT</label>"
        "<input name='c%u_campo_mqtt' value='%s' maxlength='23'>"
        "<label>Ciclo de leitura (ms)</label>"
        "<input name='c%u_ciclo_leitura_ms' value='%lu' maxlength='6'>"
        "</fieldset>",
        /* legend Canal %u */
        (unsigned)(indice_canal + 1),
        /* checkbox c%u_habilitado */
        (unsigned)indice_canal, canal->habilitado ? "checked" : "",
        /* input c%u_nome */
        (unsigned)indice_canal, canal->nome,
        /* input c%u_endereco_inicial */
        (unsigned)indice_canal, canal->endereco_inicial,
        /* input c%u_quantidade_registradores */
        (unsigned)indice_canal, canal->quantidade_registradores,
        /* select c%u_tipo_dado + 5 selected */
        (unsigned)indice_canal,
        canal->tipo_dado == MODBUS_TIPO_DADO_UINT16  ? "selected" : "",
        canal->tipo_dado == MODBUS_TIPO_DADO_INT16   ? "selected" : "",
        canal->tipo_dado == MODBUS_TIPO_DADO_UINT32  ? "selected" : "",
        canal->tipo_dado == MODBUS_TIPO_DADO_INT32   ? "selected" : "",
        canal->tipo_dado == MODBUS_TIPO_DADO_FLOAT32 ? "selected" : "",
        /* select c%u_ordem_palavra + 4 selected */
        (unsigned)indice_canal,
        canal->ordem_palavra == MODBUS_ORDEM_PALAVRA_ABCD ? "selected" : "",
        canal->ordem_palavra == MODBUS_ORDEM_PALAVRA_CDAB ? "selected" : "",
        canal->ordem_palavra == MODBUS_ORDEM_PALAVRA_BADC ? "selected" : "",
        canal->ordem_palavra == MODBUS_ORDEM_PALAVRA_DCBA ? "selected" : "",
        /* demais campos */
        (unsigned)indice_canal, (double)canal->escala,
        (unsigned)indice_canal, (double)canal->deslocamento,
        (unsigned)indice_canal, canal->casas_decimais,
        (unsigned)indice_canal, canal->unidade,
        (unsigned)indice_canal, canal->limite_minimo_habilitado ? "checked" : "",
        (unsigned)indice_canal, (double)canal->limite_minimo,
        (unsigned)indice_canal, canal->limite_maximo_habilitado ? "checked" : "",
        (unsigned)indice_canal, (double)canal->limite_maximo,
        (unsigned)indice_canal, canal->campo_mqtt,
        (unsigned)indice_canal, (unsigned long)canal->ciclo_leitura_ms);
}

static esp_err_t handler_get_rs485_dispositivo(httpd_req_t *req)
{
    if (!Config_Server_Kincony_SessaoValida(req))
    {
        return redirecionar(req, "/login");
    }

    uint8_t indice = 0;

    if (!obter_indice_da_query(req, &indice))
    {
        return redirecionar(req, "/rs485");
    }

    modbus_rs485_configuracao_t config;

    if (Modbus_RS485_ObterSnapshot(&config) != ESP_OK)
    {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    const modbus_dispositivo_t *dispositivo = &config.dispositivos[indice];
    const char *status = !dispositivo->habilitado ? "desabilitado" : (dispositivo->online ? "online" : "offline");

    httpd_resp_set_type(req, "text/html");

    // Editado por Eraldo Bispo - 28/06/2026 - buffer aumentado de 1700 para 3500: o CSS comum
    // (~1,1KB) + o HTML estatico ate o <h3>Canais</h3> (~1,3KB) ja somam ~2,4KB sem contar os
    // valores dinamicos (nome, identificador MQTT etc). Com 1700 bytes o snprintf truncava o FINAL
    // da string - exatamente os campos "Habilitado/Nome/Identificador MQTT/Endereco escravo/
    // Timeout" do dispositivo e o titulo "Canais", que ficam depois do bloco de Status no formato.
    // Por isso a pagina pulava direto do Status pro "Canal 1", parecendo que o nome do dispositivo
    // nao era editavel (o campo existe no codigo, mas nunca chegava a ser enviado pro navegador).
    char buffer_cabecalho[3500];

    snprintf(buffer_cabecalho, sizeof(buffer_cabecalho),
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width, initial-scale=1'>"
        "<title>AquaPulse - Dispositivo Modbus</title>%s"
        "<style>fieldset legend{padding:0 6px}.status{background:#fafbfc;border:1px solid #e3e6ec;"
        "border-radius:10px;padding:10px 14px;margin-bottom:16px;font-size:13px;color:#142a4d}</style>"
        "</head><body><div class='card' style='width:420px'>"
        "<img class='logo' src='/logo.jpg'>"
        "<h2>Dispositivo %u</h2>"
        "<p class='sub'>Configura\xC3\xA7\xC3\xA3o do dispositivo e seus canais</p>"
        "<div class='rodape' style='margin:-6px 0 16px'><a href='/rs485'>&larr; Voltar</a></div>"
        "<div class='status'>Status: <b>%s</b> &middot; falhas consecutivas: %lu &middot; sucessos: %lu</div>"
        "<form method='POST' action='/rs485/dispositivo'>"
        "<input type='hidden' name='indice' value='%u'>"
        "<label>Habilitado</label>"
        "<input type='checkbox' name='habilitado' value='1' %s style='width:auto;margin-bottom:16px'>"
        "<label>Nome</label>"
        "<input name='nome' value='%s' maxlength='31'>"
        "<label>Identificador MQTT</label>"
        "<input name='identificador_mqtt' value='%s' maxlength='23'>"
        "<label>Endere\xC3\xA7o escravo</label>"
        "<input name='endereco_escravo' value='%u' maxlength='3'>"
        "<label>Timeout (ms, 0 = usa o do barramento)</label>"
        "<input name='timeout_ms' value='%lu' maxlength='6'>"
        "<h3 style='color:#142a4d;font-size:15px;margin:18px 0 10px'>Canais</h3>",
        Config_Server_Kincony_ObterEstiloComum(),
        (unsigned)(indice + 1),
        status, (unsigned long)dispositivo->contador_falha_consecutiva, (unsigned long)dispositivo->contador_sucesso,
        (unsigned)indice,
        dispositivo->habilitado ? "checked" : "",
        dispositivo->nome,
        dispositivo->identificador_mqtt,
        dispositivo->endereco_escravo,
        (unsigned long)dispositivo->timeout_ms);

    httpd_resp_send_chunk(req, buffer_cabecalho, HTTPD_RESP_USE_STRLEN);

    // Editado por Eraldo Bispo - 28/06/2026 - margem aumentada de 2400 para 2900: o fieldset
    // estatico por canal ja soma ~2,05KB sem os valores dinamicos (nome, unidade, campo MQTT,
    // escala/deslocamento/limites em %g) - a folga anterior (~250 bytes) era pequena demais.
    char buffer_canal[2900];

    for (uint8_t i = 0; i < MODBUS_RS485_MAX_CANAIS_POR_DISPOSITIVO; i++)
    {
        montar_fieldset_canal(buffer_canal, sizeof(buffer_canal), i, &dispositivo->canais[i]);
        httpd_resp_send_chunk(req, buffer_canal, HTTPD_RESP_USE_STRLEN);
    }

    static const char rodape[] =
        "<button type='submit' name='acao' value='salvar'>Salvar e reiniciar</button>"
        "<button type='submit' name='acao' value='excluir' "
        "style='background:#d33333;margin-top:10px'>Excluir dispositivo</button>"
        "</form>"
        "<div class='rodape'><a href='/rs485'>Voltar</a></div>"
        "</div></body></html>";

    httpd_resp_send_chunk(req, rodape, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, NULL, 0);

    return ESP_OK;
}

// Editado por Eraldo Bispo - 23/06/2026 - monta a chave "cN_sufixo" (ex: "c2_escala") e busca no
// corpo do POST - os 4 canais do dispositivo usam o mesmo conjunto de sufixos de campo, prefixado
// pelo indice do canal dentro do formulario (ver montar_fieldset_canal acima).
static bool obter_campo_canal(const char *corpo, uint8_t indice_canal, const char *sufixo, char *destino, size_t tamanho)
{
    char chave[40];
    snprintf(chave, sizeof(chave), "c%u_%s", (unsigned)indice_canal, sufixo);
    return httpd_query_key_value(corpo, chave, destino, tamanho) == ESP_OK;
}

static esp_err_t handler_post_rs485_dispositivo(httpd_req_t *req)
{
    if (!Config_Server_Kincony_SessaoValida(req))
    {
        return redirecionar(req, "/login");
    }

    // Editado por Eraldo Bispo - 23/06/2026 - este formulario e bem maior que os outros do painel
    // (4 canais x ~13 campos) - um unico httpd_req_recv() pode nao trazer o corpo inteiro de uma
    // vez se ele vier em mais de um segmento TCP, por isso le em loop ate completar content_len.
    char corpo[3000];
    int recebido_total = 0;

    while (recebido_total < req->content_len && recebido_total < (int)sizeof(corpo) - 1)
    {
        int recebido = httpd_req_recv(req, corpo + recebido_total, sizeof(corpo) - 1 - recebido_total);

        if (recebido <= 0)
        {
            if (recebido == HTTPD_SOCK_ERR_TIMEOUT)
            {
                continue;
            }

            httpd_resp_send_500(req);
            return ESP_FAIL;
        }

        recebido_total += recebido;
    }

    corpo[recebido_total] = '\0';

    char bruto[64];

    if (httpd_query_key_value(corpo, "indice", bruto, sizeof(bruto)) != ESP_OK)
    {
        return redirecionar(req, "/rs485");
    }

    int indice_lido = atoi(bruto);

    if (indice_lido < 0 || indice_lido >= MODBUS_RS485_MAX_DISPOSITIVOS)
    {
        return redirecionar(req, "/rs485");
    }

    uint8_t indice = (uint8_t)indice_lido;

    modbus_rs485_configuracao_t config;

    if (Modbus_RS485_Config_Carregar(&config) != ESP_OK)
    {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    modbus_dispositivo_t *dispositivo = &config.dispositivos[indice];

    // Editado por Eraldo Bispo - 24/06/2026 - botao "Excluir dispositivo" (ver
    // handler_get_rs485_dispositivo): zera o slot por completo (volta a ser "vazio", disponivel
    // pra um dispositivo novo) em vez de aplicar os campos do formulario.
    if (httpd_query_key_value(corpo, "acao", bruto, sizeof(bruto)) == ESP_OK && strcmp(bruto, "excluir") == 0)
    {
        memset(dispositivo, 0, sizeof(*dispositivo));

        esp_err_t ret_exclusao = Modbus_RS485_Config_Salvar(&config);

        if (ret_exclusao != ESP_OK)
        {
            ESP_LOGE(TAG, "Erro ao excluir dispositivo %u: 0x%x", (unsigned)indice, ret_exclusao);
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }

        ESP_LOGW(TAG, "Dispositivo %u excluido via painel web", (unsigned)indice);

        return enviar_pagina_reiniciando(req);
    }

    dispositivo->habilitado = (httpd_query_key_value(corpo, "habilitado", bruto, sizeof(bruto)) == ESP_OK);

    if (httpd_query_key_value(corpo, "nome", bruto, sizeof(bruto)) == ESP_OK)
    {
        Config_Server_Kincony_UrlDecode(dispositivo->nome, bruto, sizeof(dispositivo->nome));
    }

    if (httpd_query_key_value(corpo, "identificador_mqtt", bruto, sizeof(bruto)) == ESP_OK)
    {
        Config_Server_Kincony_UrlDecode(dispositivo->identificador_mqtt, bruto, sizeof(dispositivo->identificador_mqtt));
    }

    if (httpd_query_key_value(corpo, "endereco_escravo", bruto, sizeof(bruto)) == ESP_OK)
    {
        dispositivo->endereco_escravo = (uint8_t)atoi(bruto);
    }

    if (httpd_query_key_value(corpo, "timeout_ms", bruto, sizeof(bruto)) == ESP_OK)
    {
        dispositivo->timeout_ms = (uint32_t)atol(bruto);
    }

    for (uint8_t i = 0; i < MODBUS_RS485_MAX_CANAIS_POR_DISPOSITIVO; i++)
    {
        modbus_canal_t *canal = &dispositivo->canais[i];

        canal->habilitado = obter_campo_canal(corpo, i, "habilitado", bruto, sizeof(bruto));
        canal->funcao = MODBUS_FUNCAO_LEITURA_HOLDING;

        if (obter_campo_canal(corpo, i, "nome", bruto, sizeof(bruto)))
        {
            Config_Server_Kincony_UrlDecode(canal->nome, bruto, sizeof(canal->nome));
        }

        if (obter_campo_canal(corpo, i, "endereco_inicial", bruto, sizeof(bruto)))
        {
            canal->endereco_inicial = (uint16_t)atoi(bruto);
        }

        if (obter_campo_canal(corpo, i, "quantidade_registradores", bruto, sizeof(bruto)))
        {
            canal->quantidade_registradores = (uint16_t)atoi(bruto);
        }

        if (obter_campo_canal(corpo, i, "tipo_dado", bruto, sizeof(bruto)))
        {
            canal->tipo_dado = (modbus_tipo_dado_t)atoi(bruto);
        }

        if (obter_campo_canal(corpo, i, "ordem_palavra", bruto, sizeof(bruto)))
        {
            canal->ordem_palavra = (modbus_ordem_palavra_t)atoi(bruto);
        }

        if (obter_campo_canal(corpo, i, "escala", bruto, sizeof(bruto)))
        {
            canal->escala = strtof(bruto, NULL);
        }

        if (obter_campo_canal(corpo, i, "deslocamento", bruto, sizeof(bruto)))
        {
            canal->deslocamento = strtof(bruto, NULL);
        }

        if (obter_campo_canal(corpo, i, "casas_decimais", bruto, sizeof(bruto)))
        {
            canal->casas_decimais = (uint8_t)atoi(bruto);
        }

        if (obter_campo_canal(corpo, i, "unidade", bruto, sizeof(bruto)))
        {
            Config_Server_Kincony_UrlDecode(canal->unidade, bruto, sizeof(canal->unidade));
        }

        canal->limite_minimo_habilitado = obter_campo_canal(corpo, i, "limite_minimo_habilitado", bruto, sizeof(bruto));

        if (obter_campo_canal(corpo, i, "limite_minimo", bruto, sizeof(bruto)))
        {
            canal->limite_minimo = strtof(bruto, NULL);
        }

        canal->limite_maximo_habilitado = obter_campo_canal(corpo, i, "limite_maximo_habilitado", bruto, sizeof(bruto));

        if (obter_campo_canal(corpo, i, "limite_maximo", bruto, sizeof(bruto)))
        {
            canal->limite_maximo = strtof(bruto, NULL);
        }

        if (obter_campo_canal(corpo, i, "campo_mqtt", bruto, sizeof(bruto)))
        {
            Config_Server_Kincony_UrlDecode(canal->campo_mqtt, bruto, sizeof(canal->campo_mqtt));
        }

        if (obter_campo_canal(corpo, i, "ciclo_leitura_ms", bruto, sizeof(bruto)))
        {
            canal->ciclo_leitura_ms = (uint32_t)atol(bruto);
        }
    }

    esp_err_t ret = Modbus_RS485_Config_Salvar(&config);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Erro ao salvar configuracao do dispositivo %u: 0x%x", (unsigned)indice, ret);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    return enviar_pagina_reiniciando(req);
}

void Modbus_RS485_Web_Iniciar(void)
{
    httpd_handle_t servidor = Config_Server_Kincony_ObterServidorHttp();

    if (servidor == NULL)
    {
        ESP_LOGE(TAG, "Servidor HTTP do painel ainda nao foi iniciado - paginas RS485 nao registradas");
        return;
    }

    httpd_uri_t uri_get_rs485 = { .uri = "/rs485", .method = HTTP_GET, .handler = handler_get_rs485 };
    httpd_uri_t uri_post_rs485 = { .uri = "/rs485", .method = HTTP_POST, .handler = handler_post_rs485 };
    httpd_uri_t uri_get_dispositivo = { .uri = "/rs485/dispositivo", .method = HTTP_GET, .handler = handler_get_rs485_dispositivo };
    httpd_uri_t uri_post_dispositivo = { .uri = "/rs485/dispositivo", .method = HTTP_POST, .handler = handler_post_rs485_dispositivo };

    // Editado por Eraldo Bispo - 24/06/2026 - checa o retorno: httpd_register_uri_handler()
    // falha CALADO (sem log nenhum) se passar de httpd_config_t.max_uri_handlers
    esp_err_t ret = httpd_register_uri_handler(servidor, &uri_get_rs485);
    ret |= httpd_register_uri_handler(servidor, &uri_post_rs485);
    ret |= httpd_register_uri_handler(servidor, &uri_get_dispositivo);
    ret |= httpd_register_uri_handler(servidor, &uri_post_dispositivo);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Erro ao registrar uma ou mais rotas RS485 (max_uri_handlers insuficiente?)");
        return;
    }

    ESP_LOGI(TAG, "Paginas RS485 / Modbus RTU registradas em /rs485 e /rs485/dispositivo");
}
