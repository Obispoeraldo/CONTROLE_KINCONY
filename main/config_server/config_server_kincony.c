/*
 * config_server_kincony.c
 *
 * Criado por Eraldo Bispo.
 * Painel web de configuracao do WiFi e broker MQTT, acessivel pela rede do
 * proprio ESP32. Os valores ficam salvos em NVS e sao usados como padrao de
 * fabrica apenas na primeira vez que o ESP liga (via menuconfig).
 *
 * Login customizado com a identidade visual da AquaPulse (logo embutida no
 * firmware + sessao por cookie), no lugar da caixa nativa do navegador.
 */

#include "config_server_kincony.h"

#include <string.h>
#include <stdlib.h>

#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_system.h"
#include "esp_random.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "CONFIG_SERVER"

#define NVS_NAMESPACE           "kincony_cfg"
#define NVS_KEY_WIFI_SSID       "ssid"
#define NVS_KEY_WIFI_SENHA      "senha"
#define NVS_KEY_BROKER_URI      "broker"
// Criado por Eraldo Bispo — backup do ultimo WiFi que funcionou, usado para reverter se uma troca falhar
#define NVS_KEY_WIFI_SSID_BKP   "ssid_bkp"
#define NVS_KEY_WIFI_SENHA_BKP  "senha_bkp"
// Criado por Eraldo Bispo — credenciais opcionais do broker MQTT, editaveis pelo painel web
#define NVS_KEY_MQTT_USUARIO    "mqtt_user"
#define NVS_KEY_MQTT_SENHA      "mqtt_pass"

#define TAM_SSID    32
#define TAM_SENHA   64
#define TAM_BROKER  128
#define TAM_MQTT_USUARIO 64
#define TAM_MQTT_SENHA   64

#define SESSAO_COOKIE_NOME "kincony_sessao"
#define SESSAO_TOKEN_TAM   33

static char config_wifi_ssid[TAM_SSID]    = {0};
static char config_wifi_senha[TAM_SENHA]  = {0};
static char config_broker_uri[TAM_BROKER] = {0};
static char config_mqtt_usuario[TAM_MQTT_USUARIO] = {0};
static char config_mqtt_senha[TAM_MQTT_SENHA]     = {0};

static char sessao_token[SESSAO_TOKEN_TAM] = {0};

static httpd_handle_t servidor_http = NULL;

/* Logo AquaPulse embutida no firmware (ver EMBED_FILES no main/CMakeLists.txt)
 * O simbolo usa apenas o nome do arquivo (logo_aquapulse.jpg), nao o caminho completo. */
extern const uint8_t logo_jpg_start[] asm("_binary_logo_aquapulse_jpg_start");
extern const uint8_t logo_jpg_end[]   asm("_binary_logo_aquapulse_jpg_end");

/* Estilo compartilhado entre a tela de login e a tela de configuracao, seguindo a identidade visual AquaPulse */
static const char ESTILO_AQUAPULSE[] =
    "<style>"
    "body{background:#0e2236;display:flex;justify-content:center;align-items:center;"
    "min-height:100vh;margin:0;font-family:-apple-system,Segoe UI,Roboto,sans-serif}"
    ".card{background:#fff;border-radius:24px;padding:40px 32px;width:320px;"
    "box-shadow:0 20px 50px rgba(0,0,0,.35)}"
    ".logo{display:block;margin:0 auto 16px;width:120px}"
    "h2{text-align:center;color:#142a4d;margin:0 0 6px;font-size:23px}"
    "p.sub{text-align:center;color:#8a93a3;margin:0 0 22px;font-size:13px}"
    "label{display:block;color:#142a4d;font-weight:600;font-size:13px;margin-bottom:6px}"
    "input{width:100%;padding:12px 14px;border:1px solid #e3e6ec;border-radius:10px;"
    "margin-bottom:16px;font-size:14px;box-sizing:border-box;background:#fafbfc}"
    "input:focus{outline:none;border-color:#16a3b8}"
    "button{width:100%;background:#0e93a6;color:#fff;border:none;padding:13px;"
    "border-radius:10px;font-size:15px;font-weight:600;cursor:pointer}"
    "button:hover{background:#0c7e8f}"
    ".erro{color:#d33333;text-align:center;font-size:13px;margin:-6px 0 16px}"
    ".rodape{text-align:center;margin-top:16px;font-size:13px}"
    ".rodape a{color:#0e93a6;text-decoration:none}"
    "</style>";

static esp_err_t carregar_ou_gravar_default(
    nvs_handle_t nvs,
    const char *chave,
    char *destino,
    size_t tamanho_destino,
    const char *valor_default
)
{
    size_t tamanho = tamanho_destino;
    esp_err_t ret = nvs_get_str(nvs, chave, destino, &tamanho);

    if (ret == ESP_ERR_NVS_NOT_FOUND)
    {
        strncpy(destino, valor_default, tamanho_destino - 1);
        destino[tamanho_destino - 1] = '\0';

        nvs_set_str(nvs, chave, destino);
        nvs_commit(nvs);

        ESP_LOGI(TAG, "Chave '%s' nao existia em NVS, gravado valor padrao do menuconfig", chave);

        return ESP_OK;
    }

    return ret;
}

esp_err_t Config_Server_Kincony_Iniciar(void)
{
    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Erro ao iniciar NVS");
        return ret;
    }

    nvs_handle_t nvs;
    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Erro ao abrir namespace NVS '%s'", NVS_NAMESPACE);
        return ret;
    }

    carregar_ou_gravar_default(nvs, NVS_KEY_WIFI_SSID, config_wifi_ssid, sizeof(config_wifi_ssid), CONFIG_KINCONY_WIFI_SSID);
    carregar_ou_gravar_default(nvs, NVS_KEY_WIFI_SENHA, config_wifi_senha, sizeof(config_wifi_senha), CONFIG_KINCONY_WIFI_PASSWORD);
    carregar_ou_gravar_default(nvs, NVS_KEY_BROKER_URI, config_broker_uri, sizeof(config_broker_uri), CONFIG_KINCONY_MQTT_BROKER_URI);
    carregar_ou_gravar_default(nvs, NVS_KEY_MQTT_USUARIO, config_mqtt_usuario, sizeof(config_mqtt_usuario), CONFIG_KINCONY_MQTT_USER);
    carregar_ou_gravar_default(nvs, NVS_KEY_MQTT_SENHA, config_mqtt_senha, sizeof(config_mqtt_senha), CONFIG_KINCONY_MQTT_PASSWORD);

    nvs_close(nvs);

    ESP_LOGI(TAG, "Configuracao carregada. SSID: %s | Broker: %s", config_wifi_ssid, config_broker_uri);

    return ESP_OK;
}

void Config_Server_Kincony_GetWifiSsid(char *destino, size_t tamanho)
{
    strncpy(destino, config_wifi_ssid, tamanho - 1);
    destino[tamanho - 1] = '\0';
}

void Config_Server_Kincony_GetWifiSenha(char *destino, size_t tamanho)
{
    strncpy(destino, config_wifi_senha, tamanho - 1);
    destino[tamanho - 1] = '\0';
}

void Config_Server_Kincony_GetBrokerUri(char *destino, size_t tamanho)
{
    strncpy(destino, config_broker_uri, tamanho - 1);
    destino[tamanho - 1] = '\0';
}

void Config_Server_Kincony_GetMqttUsuario(char *destino, size_t tamanho)
{
    strncpy(destino, config_mqtt_usuario, tamanho - 1);
    destino[tamanho - 1] = '\0';
}

void Config_Server_Kincony_GetMqttSenha(char *destino, size_t tamanho)
{
    strncpy(destino, config_mqtt_senha, tamanho - 1);
    destino[tamanho - 1] = '\0';
}

esp_err_t Config_Server_Kincony_RestaurarBackupWifi(void)
{
    nvs_handle_t nvs;

    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK)
    {
        return ESP_FAIL;
    }

    char ssid_backup[TAM_SSID]   = {0};
    char senha_backup[TAM_SENHA] = {0};
    size_t tamanho_ssid = sizeof(ssid_backup);
    size_t tamanho_senha = sizeof(senha_backup);

    esp_err_t ret_ssid = nvs_get_str(nvs, NVS_KEY_WIFI_SSID_BKP, ssid_backup, &tamanho_ssid);
    esp_err_t ret_senha = nvs_get_str(nvs, NVS_KEY_WIFI_SENHA_BKP, senha_backup, &tamanho_senha);

    if (ret_ssid != ESP_OK || ret_senha != ESP_OK || strlen(ssid_backup) == 0)
    {
        nvs_close(nvs);
        return ESP_FAIL;
    }

    nvs_set_str(nvs, NVS_KEY_WIFI_SSID, ssid_backup);
    nvs_set_str(nvs, NVS_KEY_WIFI_SENHA, senha_backup);

    // Limpa o backup para nao reverter em loop caso o WiFi anterior tambem tenha algum problema
    nvs_erase_key(nvs, NVS_KEY_WIFI_SSID_BKP);
    nvs_erase_key(nvs, NVS_KEY_WIFI_SENHA_BKP);

    nvs_commit(nvs);
    nvs_close(nvs);

    ESP_LOGW(TAG, "WiFi revertido para a configuracao anterior: %s", ssid_backup);

    return ESP_OK;
}

static void url_decode(char *destino, const char *origem, size_t tamanho_destino)
{
    size_t i = 0, j = 0;

    while (origem[i] != '\0' && j < tamanho_destino - 1)
    {
        if (origem[i] == '+')
        {
            destino[j++] = ' ';
            i++;
        }
        else if (origem[i] == '%' && origem[i + 1] != '\0' && origem[i + 2] != '\0')
        {
            char hex[3] = { origem[i + 1], origem[i + 2], '\0' };
            destino[j++] = (char)strtol(hex, NULL, 16);
            i += 3;
        }
        else
        {
            destino[j++] = origem[i++];
        }
    }

    destino[j] = '\0';
}

static void gerar_token_sessao(char *destino, size_t tamanho)
{
    for (size_t i = 0; i + 1 < tamanho; i += 2)
    {
        uint8_t byte = (uint8_t)(esp_random() & 0xFF);
        snprintf(destino + i, 3, "%02x", byte);
    }
}

static bool extrair_cookie(const char *cabecalho, const char *nome, char *destino, size_t tamanho_destino)
{
    char busca[48];
    snprintf(busca, sizeof(busca), "%s=", nome);

    const char *inicio = strstr(cabecalho, busca);

    if (inicio == NULL)
    {
        return false;
    }

    inicio += strlen(busca);

    const char *fim = strchr(inicio, ';');
    size_t tamanho_valor = (fim != NULL) ? (size_t)(fim - inicio) : strlen(inicio);

    if (tamanho_valor >= tamanho_destino)
    {
        tamanho_valor = tamanho_destino - 1;
    }

    memcpy(destino, inicio, tamanho_valor);
    destino[tamanho_valor] = '\0';

    return true;
}

static bool sessao_valida(httpd_req_t *req)
{
    if (strlen(sessao_token) == 0)
    {
        return false;
    }

    char cabecalho_cookie[96] = {0};

    if (httpd_req_get_hdr_value_str(req, "Cookie", cabecalho_cookie, sizeof(cabecalho_cookie)) != ESP_OK)
    {
        return false;
    }

    char valor[SESSAO_TOKEN_TAM] = {0};

    if (!extrair_cookie(cabecalho_cookie, SESSAO_COOKIE_NOME, valor, sizeof(valor)))
    {
        return false;
    }

    return (strcmp(valor, sessao_token) == 0);
}

static esp_err_t redirecionar(httpd_req_t *req, const char *destino)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", destino);
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t handler_logo(httpd_req_t *req)
{
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_send(req, (const char *)logo_jpg_start, logo_jpg_end - logo_jpg_start);
    return ESP_OK;
}

static esp_err_t handler_get_login(httpd_req_t *req)
{
    bool mostrar_erro = false;

    char query[16];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK)
    {
        mostrar_erro = (strstr(query, "erro=1") != NULL);
    }

    char pagina[2400];

    snprintf(pagina, sizeof(pagina),
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width, initial-scale=1'>"
        "<title>AquaPulse - Entrar</title>%s</head>"
        "<body><div class='card'>"
        "<img class='logo' src='/logo.jpg'>"
        "<h2>Entrar</h2>"
        "<p class='sub'>Acesse o painel de configura\xC3\xA7\xC3\xA3o do ESP32.</p>"
        "%s"
        "<form method='POST' action='/login'>"
        "<label>Usu\xC3\xA1rio</label>"
        "<input name='usuario' autocomplete='username'>"
        "<label>Senha</label>"
        "<input name='senha' type='password' autocomplete='current-password'>"
        "<button type='submit'>Entrar</button>"
        "</form></div></body></html>",
        ESTILO_AQUAPULSE,
        mostrar_erro ? "<p class='erro'>Usu\xC3\xA1rio ou senha inv\xC3\xA1lidos</p>" : "");

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, pagina, HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}

static esp_err_t handler_post_login(httpd_req_t *req)
{
    char corpo[160] = {0};
    int recebido = httpd_req_recv(req, corpo, sizeof(corpo) - 1);

    if (recebido <= 0)
    {
        return redirecionar(req, "/login?erro=1");
    }

    corpo[recebido] = '\0';

    char usuario_bruto[32] = {0};
    char senha_bruta[64]   = {0};

    httpd_query_key_value(corpo, "usuario", usuario_bruto, sizeof(usuario_bruto));
    httpd_query_key_value(corpo, "senha", senha_bruta, sizeof(senha_bruta));

    char usuario[32];
    char senha[64];

    url_decode(usuario, usuario_bruto, sizeof(usuario));
    url_decode(senha, senha_bruta, sizeof(senha));

    if (strcmp(usuario, CONFIG_KINCONY_HTTP_USER) != 0 || strcmp(senha, CONFIG_KINCONY_HTTP_PASSWORD) != 0)
    {
        ESP_LOGW(TAG, "Tentativa de login invalida. Usuario informado: %s", usuario);
        return redirecionar(req, "/login?erro=1");
    }

    gerar_token_sessao(sessao_token, sizeof(sessao_token));

    // Editado por Eraldo Bispo — buffer aumentado: nome(14) + '=' + token(32) + "; Path=/; HttpOnly"(18) + '\0' = 66 bytes
    char cookie[80];
    snprintf(cookie, sizeof(cookie), "%s=%s; Path=/; HttpOnly", SESSAO_COOKIE_NOME, sessao_token);

    httpd_resp_set_hdr(req, "Set-Cookie", cookie);

    ESP_LOGI(TAG, "Login efetuado no painel de configuracao");

    return redirecionar(req, "/");
}

static esp_err_t handler_logout(httpd_req_t *req)
{
    sessao_token[0] = '\0';
    return redirecionar(req, "/login");
}

static esp_err_t handler_get_config(httpd_req_t *req)
{
    if (!sessao_valida(req))
    {
        return redirecionar(req, "/login");
    }

    char pagina[3000];

    snprintf(pagina, sizeof(pagina),
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width, initial-scale=1'>"
        "<title>AquaPulse - Configura\xC3\xA7\xC3\xA3o</title>%s</head>"
        "<body><div class='card'>"
        "<img class='logo' src='/logo.jpg'>"
        "<h2>Configura\xC3\xA7\xC3\xA3o</h2>"
        "<p class='sub'>Painel de controle dos aeradores</p>"
        "<form method='POST' action='/salvar'>"
        "<label>WiFi SSID</label>"
        "<input name='ssid' value='%s' maxlength='31'>"
        "<label>WiFi Senha (deixe vazio para manter a atual)</label>"
        "<input name='senha' type='password' maxlength='63'>"
        "<label>Broker MQTT</label>"
        "<input name='broker' value='%s' maxlength='127'>"
        "<label>Usu\xC3\xA1rio do broker MQTT (deixe vazio se nao precisar)</label>"
        "<input name='mqtt_usuario' value='%s' maxlength='63'>"
        "<label>Senha do broker MQTT (deixe vazio para manter a atual)</label>"
        "<input name='mqtt_senha' type='password' maxlength='63'>"
        "<button type='submit'>Salvar e reiniciar</button>"
        "</form>"
        "<div class='rodape'><a href='/logout'>Sair</a></div>"
        "</div></body></html>",
        ESTILO_AQUAPULSE, config_wifi_ssid, config_broker_uri, config_mqtt_usuario);

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, pagina, HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}

static esp_err_t handler_post_config(httpd_req_t *req)
{
    if (!sessao_valida(req))
    {
        return redirecionar(req, "/login");
    }

    char corpo[600] = {0};
    int recebido = httpd_req_recv(req, corpo, sizeof(corpo) - 1);

    if (recebido <= 0)
    {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    corpo[recebido] = '\0';

    char ssid_bruto[TAM_SSID]               = {0};
    char senha_bruta[TAM_SENHA]              = {0};
    char broker_bruto[TAM_BROKER]            = {0};
    char mqtt_usuario_bruto[TAM_MQTT_USUARIO] = {0};
    char mqtt_senha_bruta[TAM_MQTT_SENHA]     = {0};

    httpd_query_key_value(corpo, "ssid", ssid_bruto, sizeof(ssid_bruto));
    httpd_query_key_value(corpo, "senha", senha_bruta, sizeof(senha_bruta));
    httpd_query_key_value(corpo, "broker", broker_bruto, sizeof(broker_bruto));
    httpd_query_key_value(corpo, "mqtt_usuario", mqtt_usuario_bruto, sizeof(mqtt_usuario_bruto));
    httpd_query_key_value(corpo, "mqtt_senha", mqtt_senha_bruta, sizeof(mqtt_senha_bruta));

    char novo_ssid[TAM_SSID];
    char nova_senha[TAM_SENHA];
    char novo_broker[TAM_BROKER];
    char novo_mqtt_usuario[TAM_MQTT_USUARIO];
    char nova_mqtt_senha[TAM_MQTT_SENHA];

    url_decode(novo_ssid, ssid_bruto, sizeof(novo_ssid));
    url_decode(nova_senha, senha_bruta, sizeof(nova_senha));
    url_decode(novo_broker, broker_bruto, sizeof(novo_broker));
    url_decode(novo_mqtt_usuario, mqtt_usuario_bruto, sizeof(novo_mqtt_usuario));
    url_decode(nova_mqtt_senha, mqtt_senha_bruta, sizeof(nova_mqtt_senha));

    nvs_handle_t nvs;

    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK)
    {
        bool alterando_wifi = (strlen(novo_ssid) > 0 || strlen(nova_senha) > 0);

        // Editado por Eraldo Bispo — guarda o WiFi atual (que esta funcionando, ja que o painel esta acessivel)
        // como backup antes de aplicar a troca, para o main.c poder reverter se a nova rede falhar
        if (alterando_wifi)
        {
            nvs_set_str(nvs, NVS_KEY_WIFI_SSID_BKP, config_wifi_ssid);
            nvs_set_str(nvs, NVS_KEY_WIFI_SENHA_BKP, config_wifi_senha);
        }

        if (strlen(novo_ssid) > 0)
        {
            nvs_set_str(nvs, NVS_KEY_WIFI_SSID, novo_ssid);
            strncpy(config_wifi_ssid, novo_ssid, sizeof(config_wifi_ssid) - 1);
        }

        if (strlen(nova_senha) > 0)
        {
            nvs_set_str(nvs, NVS_KEY_WIFI_SENHA, nova_senha);
            strncpy(config_wifi_senha, nova_senha, sizeof(config_wifi_senha) - 1);
        }

        if (strlen(novo_broker) > 0)
        {
            nvs_set_str(nvs, NVS_KEY_BROKER_URI, novo_broker);
            strncpy(config_broker_uri, novo_broker, sizeof(config_broker_uri) - 1);
        }

        // Editado por Eraldo Bispo — usuario do broker MQTT (campo pre-preenchido, igual ssid/broker)
        if (strlen(novo_mqtt_usuario) > 0)
        {
            nvs_set_str(nvs, NVS_KEY_MQTT_USUARIO, novo_mqtt_usuario);
            strncpy(config_mqtt_usuario, novo_mqtt_usuario, sizeof(config_mqtt_usuario) - 1);
        }

        // Editado por Eraldo Bispo — senha do broker MQTT (campo vazio = manter a atual, igual senha WiFi)
        if (strlen(nova_mqtt_senha) > 0)
        {
            nvs_set_str(nvs, NVS_KEY_MQTT_SENHA, nova_mqtt_senha);
            strncpy(config_mqtt_senha, nova_mqtt_senha, sizeof(config_mqtt_senha) - 1);
        }

        nvs_commit(nvs);
        nvs_close(nvs);
    }

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req,
        "<html><body style='font-family:sans-serif;background:#0e2236;color:#fff;"
        "display:flex;justify-content:center;align-items:center;min-height:100vh'>"
        "<h3>Configura\xC3\xA7\xC3\xA3o salva. Reiniciando o ESP32...</h3>"
        "</body></html>", HTTPD_RESP_USE_STRLEN);

    ESP_LOGW(TAG, "Configuracao alterada via painel web. Reiniciando em 2 segundos...");

    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();

    return ESP_OK;
}

esp_err_t Config_Server_Kincony_IniciarHttp(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    // Editado por Eraldo Bispo — o padrao (4096) estava estourando a pilha da task do
    // servidor HTTP: os handlers do painel usam buffers locais grandes (pagina[3000],
    // corpo[600]), e isso sozinho ja consumia quase toda a pilha padrao, causando reboot
    // por stack overflow (vApplicationStackOverflowHook) ao acessar "/" ou "/salvar".
    config.stack_size = 8192;

    esp_err_t ret = httpd_start(&servidor_http, &config);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Erro ao iniciar servidor HTTP de configuracao");
        return ret;
    }

    httpd_uri_t uri_logo = { .uri = "/logo.jpg", .method = HTTP_GET, .handler = handler_logo };
    httpd_uri_t uri_get_login = { .uri = "/login", .method = HTTP_GET, .handler = handler_get_login };
    httpd_uri_t uri_post_login = { .uri = "/login", .method = HTTP_POST, .handler = handler_post_login };
    httpd_uri_t uri_logout = { .uri = "/logout", .method = HTTP_GET, .handler = handler_logout };
    httpd_uri_t uri_get_config = { .uri = "/", .method = HTTP_GET, .handler = handler_get_config };
    httpd_uri_t uri_post_config = { .uri = "/salvar", .method = HTTP_POST, .handler = handler_post_config };

    httpd_register_uri_handler(servidor_http, &uri_logo);
    httpd_register_uri_handler(servidor_http, &uri_get_login);
    httpd_register_uri_handler(servidor_http, &uri_post_login);
    httpd_register_uri_handler(servidor_http, &uri_logout);
    httpd_register_uri_handler(servidor_http, &uri_get_config);
    httpd_register_uri_handler(servidor_http, &uri_post_config);

    ESP_LOGI(TAG, "Painel de configuracao disponivel em http://<IP_DO_ESP>/  (ver IP no log acima)");

    return ESP_OK;
}
