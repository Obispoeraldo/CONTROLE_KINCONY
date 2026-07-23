#include "entradas_kincony.h"
#include "saidas_digitais_kincony.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "modbus_rtu_slave_esp.h"
#include "mqtt_kincony.h"
#include "wifi_kincony.h"
#include "wifi_config_button.h"
#include "ota_github.h"
#include "logica_controle.h"
#include "config_server_kincony.h"
// Editado por Eraldo Bispo - 23/06/2026 - substitui o teste fixo do Novus N1200 pelo motor
// Modbus RTU master generico e configuravel (N dispositivos x canais, ver modbus_rs485_config.h)
#include "modbus_rs485_master.h"
#include "modbus_rs485_mqtt.h"
#include "modbus_rs485_web.h"
#include "ntp_kincony.h"
// Criado por Daniel Montanher - RTC DS1307 com bateria + 4 timers persistentes de aeradores.
// Integrado por Eraldo Bispo e Daniel Montanher a um servico de tempo unico com o NTP acima
// (ver docs/RELATORIO_INTEGRACAO_ERALDO_DANIEL.md, secao "Servico de tempo").
#include "rtc_ds1307.h"

void app_main(void)
{
    // Inicialização dos componentes
    ESP_ERROR_CHECK(Entradas_Kincony_Iniciar());
    ESP_ERROR_CHECK(Saidas_Kincony_Iniciar());

    Logica_Controle_Iniciar();

    // Criado por Daniel Montanher - detecta o DS1307, carrega os 4 timers de horario da NVS e
    // aplica a hora do RTC ao relogio do sistema (disponivel mesmo sem WiFi/NTP). Chamado apos
    // Entradas_Kincony_Iniciar() porque este e quem instala o driver I2C (I2C_NUM_0) usado pelo
    // RTC. Nao falha o boot se o chip nao responder (placa sem RTC fisico) - ver comentario em
    // RTC_DS1307_Iniciar().
    ESP_ERROR_CHECK(RTC_DS1307_Iniciar());

    // Criado por Eraldo Bispo — carrega WiFi/broker da NVS (ou do menuconfig na primeira vez)
    ESP_ERROR_CHECK(Config_Server_Kincony_Iniciar());

    char wifi_ssid[32];
    char wifi_senha[64];
    char broker_uri[128];
    char mqtt_usuario[64];
    char mqtt_senha[64];
    char ntp_servidor[64];

    Config_Server_Kincony_GetWifiSsid(wifi_ssid, sizeof(wifi_ssid));
    Config_Server_Kincony_GetWifiSenha(wifi_senha, sizeof(wifi_senha));
    Config_Server_Kincony_GetBrokerUri(broker_uri, sizeof(broker_uri));
    Config_Server_Kincony_GetMqttUsuario(mqtt_usuario, sizeof(mqtt_usuario));
    Config_Server_Kincony_GetMqttSenha(mqtt_senha, sizeof(mqtt_senha));
    Config_Server_Kincony_GetNtpServidor(ntp_servidor, sizeof(ntp_servidor));

    //inicializa o wifi antes do mqtt para garantir que a conexão esteja pronta
    ESP_ERROR_CHECK(Wifi_Kincony_Init(wifi_ssid, wifi_senha));

    // Criado por Eraldo Bispo - inicializa o botao fisico de configuracao (GPIO0/BOOT - ver
    // wifi_config_button.h). Le a cada volta do loop, sem bloquear.
    ESP_ERROR_CHECK(Wifi_Config_Button_Iniciar());

    // Editado por Eraldo Bispo - espera curta so para o log do boot informar se a conexao
    // inicial foi rapida - NAO decide mais se o modulo "desiste" da rede: a reconexao continua
    // para sempre em segundo plano (backoff progressivo 30s/60s/5min, ver wifi_kincony.c) e o
    // portal de configuracao abre sozinho apos alguns minutos sem conexao, ou a qualquer
    // momento pelo botao fisico - nao ha mais timeout global nem necessidade de escolher entre
    // "esperar a rede antiga" e "poder configurar uma nova" (ver docs/RELATORIO_WIFI_RECONEXAO.md).
    if (!Wifi_Kincony_EsperarResultado(8000))
    {
        // Se a rede configurada pelo painel falhou logo no boot e existe um backup do ultimo
        // WiFi que funcionou (salvo automaticamente em trocas anteriores pelo painel), tenta
        // reverter para ele. Sem backup, so segue o boot normalmente - Wifi_Kincony_Processar()
        // no loop abaixo abre o portal sozinho se a desconexao persistir.
        if (Config_Server_Kincony_RestaurarBackupWifi() == ESP_OK)
        {
            esp_restart();
        }
    }

    // Criado por Eraldo Bispo - 24/06/2026 - sincroniza data/hora pela internet (SNTP). Unico
    // cliente SNTP do projeto: a cada sincronizacao bem-sucedida, tambem corrige o RTC DS1307 de
    // Daniel (quando presente) via callback - ver Ntp_Kincony_Iniciar() em ntp_kincony.c e
    // RTC_DS1307_NotificarSincronizacaoNtp() em rtc_ds1307.c. So faz sentido depois que o WiFi
    // conectou (ou caiu no AP de emergencia, onde nao ha internet e a hora fica a cargo do RTC
    // fisico, se presente, ou "Sincronizando..." caso contrario). Nao bloqueia o boot.
    Ntp_Kincony_Iniciar(ntp_servidor, Config_Server_Kincony_GetNtpFusoHoras());

    // Criado por Eraldo Bispo — painel web de configuracao, acessivel pelo IP do ESP (ver no monitor)
    // apos conectar. Login padrao: usuario "aquapulse", senha "aquapulse2026" (alteravel no menuconfig)
    ESP_ERROR_CHECK(Config_Server_Kincony_IniciarHttp());

    //inicia mqtt
    Mqtt_Kincony_Init(broker_uri, mqtt_usuario, mqtt_senha);
    // Editado por Eraldo Bispo - 11/07/2026 - aplica o intervalo de publicacao salvo em NVS
    // (configuravel pelo painel web sem reinicializar, ver handler_post_intervalo_mqtt).
    Mqtt_Kincony_SetIntervaloMs(Config_Server_Kincony_GetMqttIntervaloMs());

    // Editado por Eraldo Bispo — OTA agora roda em task propria com pilha dedicada (10240 bytes),
    // em vez de chamado direto no loop da main. TLS+HTTP+cJSON do OTA estavam estourando a pilha
    // da task main (vApplicationStackOverflowHook).
    Ota_Github_IniciarTask();

    // Editado por Eraldo Bispo - 23/06/2026 - Modbus RTU master generico na RS485 (substitui o
    // teste fixo do Novus N1200). Sobe sua propria task dedicada quando habilitado na config
    // (ver modbus_rs485_config.c). Nao chamar junto com ModbusRTU_Slave_Init() (mesmo barramento RS485).
    ESP_ERROR_CHECK(Modbus_RS485_Iniciar());
    Modbus_RS485_Mqtt_Iniciar();

    // Criado por Eraldo Bispo - 23/06/2026 - pagina web "RS485 / Modbus RTU" (editar barramento,
    // dispositivos e canais pelo painel, sem precisar regravar o firmware). Registra rotas no
    // MESMO servidor HTTP do painel de WiFi/MQTT (ja iniciado acima) - ver modbus_rs485_web.h.
    Modbus_RS485_Web_Iniciar();

    while (1)
    {
    // Processar entradas digitais
    Entradas_Kincony_Processar();
    // Processar lógica de controle do sistema
    Logica_Controle_Processar();
    // Editado por Eraldo Bispo - 18/06/2026 22:17 - Atualiza a saida 06 (alarme do controlador)
    // com base na falha geral (grupos, entradas/saidas offline) e na conexao MQTT/WiFi. Ver
    // motivo completo no comentario de Logica_Controle_AtualizarAlarme() em logica_controle.c.
    Logica_Controle_AtualizarAlarme(Mqtt_Kincony_IsConectado(), Wifi_Kincony_IsConectado());
    // Criado por Daniel Montanher - executa os 4 timers de horario dos aeradores (le o DS1307
    // ~1x/segundo, so age uma vez por minuto). Nao faz nada se o RTC fisico nao estiver
    // disponivel nesta placa (ver RTC_DS1307_EstaDisponivel()).
    RTC_DS1307_Processar();
    // Criado por Eraldo Bispo - reconexao/portal de configuracao WiFi (ver
    // docs/RELATORIO_WIFI_RECONEXAO.md). Nenhuma das duas bloqueia: Wifi_Kincony_Processar()
    // so verifica um temporizador interno (abre o portal sozinho apos desconexao prolongada);
    // Wifi_Config_Button_Processar() so le o nivel do GPIO0 (botao BOOT).
    Wifi_Kincony_Processar();
    Wifi_Config_Button_Processar();
    // Editado por Eraldo Bispo - 23/06/2026 - publica em MQTT o estado do barramento RS485 e dos
    // dispositivos Modbus configurados (a leitura em si roda na task dedicada, ver modbus_rs485_master.c)
    Modbus_RS485_Mqtt_Processar();
    // Processar MQTT (publicação de monitoramento e recebimento de comandos)
    Mqtt_Kincony_Processar();

    // Editado por Eraldo Bispo - 18/06/2026 22:17 - Reduzido de 5000ms para 200ms. O comando de
    // ligar/desligar grupo (via MQTT) so e aplicado na saida dentro de Logica_Controle_Processar(),
    // chamado uma vez por volta deste loop; com o delay de 5s, o comando ficava ate 5s "parado"
    // esperando a proxima volta antes do motor realmente partir. O timeout de confirmacao de
    // partida/parada (LOGICA_TIMEOUT_PARTIDA_MS / LOGICA_TIMEOUT_PARADA_MS, 5000ms) continua sendo
    // so o prazo para o feedback confirmar - a saida agora liga assim que o comando chega (no
    // maximo 200ms de atraso), e so cai em FALHA se o feedback nao confirmar dentro do timeout.
    // Entradas_Kincony_Processar() e Mqtt_Kincony_Processar() ja se autolimitam internamente
    // (TEMPO_LEITURA_MS / MQTT_PUBLICACAO_MONITORAMENTO_MS), entao rodar o loop mais rapido nao
    // sobrecarrega o I2C nem o MQTT. RTC_DS1307_Processar() tambem se autolimita (1x/segundo).
    vTaskDelay(pdMS_TO_TICKS(200));
    }
}
