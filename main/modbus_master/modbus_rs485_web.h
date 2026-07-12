#ifndef MODBUS_RS485_WEB_H
#define MODBUS_RS485_WEB_H

/*
 * modbus_rs485_web.h
 *
 * Criado por Eraldo Bispo - 23/06/2026
 *
 * Paginas web (HTML simples, sem JS, recarrega a pagina) para editar a
 * configuracao do barramento Modbus RS485 e de cada dispositivo/canal (ver
 * modbus_rs485_config.h) - antes so era possivel mudando o codigo em
 * carregar_padrao_fabrica() e regravando o firmware.
 *
 * Reaproveita o MESMO servidor HTTP e a MESMA sessao/login do painel de
 * WiFi/MQTT (ver Config_Server_Kincony_ObterServidorHttp() e
 * Config_Server_Kincony_SessaoValida() em config_server_kincony.h) - nao
 * sobe um segundo servidor nem uma segunda tela de login.
 *
 * Paginas:
 *   GET/POST /rs485                       -> configuracao do barramento +
 *                                             lista resumida dos dispositivos
 *   GET/POST /rs485/dispositivo?indice=N  -> formulario completo de UM
 *                                             dispositivo (campos do
 *                                             dispositivo + seus 4 canais)
 *
 * Salvar grava em NVS (Modbus_RS485_Config_Salvar(), ja existente) e reinicia
 * o Kincony (esp_restart()) - mesmo padrao do painel de WiFi/MQTT. Nao existe
 * "hot reload" do motor Modbus rodando.
 *
 * Function code nao e editavel aqui: o motor executa apenas leitura (FC03).
 * Todo canal salvo por este formulario e gravado como MODBUS_FUNCAO_LEITURA_HOLDING.
 *
 * Chamar uma vez, depois de Config_Server_Kincony_IniciarHttp() (servidor
 * HTTP precisa existir) e de Modbus_RS485_Iniciar() (config precisa estar
 * carregada).
 */

void Modbus_RS485_Web_Iniciar(void);

#endif
