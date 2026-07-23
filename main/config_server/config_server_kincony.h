#ifndef CONFIG_SERVER_KINCONY_H
#define CONFIG_SERVER_KINCONY_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_http_server.h"

/*
 * config_server_kincony.h
 *
 * Criado por Eraldo Bispo.
 * Guarda WiFi e broker MQTT em NVS (sobrevive a reinicializacoes), usando os
 * valores do menuconfig (Kconfig.projbuild) apenas como padrao de fabrica na
 * primeira vez que o ESP liga. Expoe um painel web (HTTP) para editar esses
 * valores remotamente, protegido por usuario/senha.
 *
 * Ordem de uso no main.c:
 *   1. Config_Server_Kincony_Iniciar()      -> inicializa NVS e carrega valores
 *   2. Wifi_Kincony_Init(ssid, senha)        -> usa os getters abaixo
 *   3. Config_Server_Kincony_IniciarHttp()   -> inicia o painel (depois do WiFi)
 *   4. Mqtt_Kincony_Init(broker, usuario, senha) -> usa os getters de broker/credenciais MQTT
 *
 * Acesso ao painel: http://<IP_DO_ESP>/  (IP aparece no log do monitor serial
 * apos a mensagem "WiFi conectado").
 * Login padrao: usuario "aquapulse", senha "aquapulse2026".
 */

esp_err_t Config_Server_Kincony_Iniciar(void);
esp_err_t Config_Server_Kincony_IniciarHttp(void);

void Config_Server_Kincony_GetWifiSsid(char *destino, size_t tamanho);
void Config_Server_Kincony_GetWifiSenha(char *destino, size_t tamanho);
void Config_Server_Kincony_GetBrokerUri(char *destino, size_t tamanho);

// Criado por Eraldo Bispo — credenciais opcionais do broker MQTT, editaveis pelo painel web
void Config_Server_Kincony_GetMqttUsuario(char *destino, size_t tamanho);
void Config_Server_Kincony_GetMqttSenha(char *destino, size_t tamanho);

// Criado por Eraldo Bispo - 24/06/2026 - servidor/fuso horario do NTP (ver ntp_kincony.h),
// editaveis pelo painel web na pagina /configuracoes
void Config_Server_Kincony_GetNtpServidor(char *destino, size_t tamanho);
int8_t Config_Server_Kincony_GetNtpFusoHoras(void);

// Criado por Eraldo Bispo - 11/07/2026 - intervalo de publicacao MQTT (ms), editavel pelo painel
// web sem reinicializar. Retorna o valor salvo em NVS clampeado em [1000, 180000].
uint32_t Config_Server_Kincony_GetMqttIntervaloMs(void);

// Criado por Eraldo Bispo — restaura o WiFi anterior (salvo automaticamente antes de qualquer troca pelo painel).
// Retorna ESP_OK se havia um backup e foi restaurado, ESP_FAIL se nao havia nada para restaurar.
esp_err_t Config_Server_Kincony_RestaurarBackupWifi(void);

// Criado por Eraldo Bispo - usado pelo botao fisico de configuracao (10s, ver
// wifi_config_button.c): apaga so as credenciais WiFi (atuais e backup) da NVS, sem tocar em
// broker MQTT/NTP. Util quando o SSID/senha salvos nao existem mais no local.
esp_err_t Config_Server_Kincony_ApagarCredenciaisWifi(void);

// Criado por Eraldo Bispo - usado pelo botao fisico de configuracao (30s, ver
// wifi_config_button.c): apaga TODO o namespace de configuracao (WiFi, broker MQTT, NTP,
// intervalo de publicacao) - o proximo boot volta a usar os padroes do menuconfig. Nao
// reinicia sozinho - quem chama decide quando reiniciar.
esp_err_t Config_Server_Kincony_RestaurarConfiguracaoFabrica(void);

// Editado por Eraldo Bispo - 23/06/2026 - utilitarios do painel web (antes "static", privados
// deste arquivo) expostos para outros paineis (ex: modbus_rs485_web.c, pagina "RS485 / Modbus
// RTU") poderem registrar rotas no MESMO servidor HTTP e reaproveitar a MESMA sessao/estilo, em
// vez de duplicar login, CSS e parsing de formulario.

// Handle do unico servidor HTTP do projeto (criado em Config_Server_Kincony_IniciarHttp()).
// NULL se o servidor ainda nao foi iniciado.
httpd_handle_t Config_Server_Kincony_ObterServidorHttp(void);

// Confere se a requisicao tem o cookie de sessao valido (login feito em /login). Mesma sessao
// usada pelo painel de WiFi/MQTT - nao ha login separado por painel.
bool Config_Server_Kincony_SessaoValida(httpd_req_t *req);

// CSS compartilhado (identidade visual AquaPulse) usado por todas as paginas do painel web.
const char *Config_Server_Kincony_ObterEstiloComum(void);

// Decodifica um valor de formulario "application/x-www-form-urlencoded" (trata '+' e '%XX').
void Config_Server_Kincony_UrlDecode(char *destino, const char *origem, size_t tamanho_destino);

#endif
