#ifndef MODBUS_RS485_MQTT_H
#define MODBUS_RS485_MQTT_H

/*
 * modbus_rs485_mqtt.h
 *
 * Criado por Eraldo Bispo - 23/06/2026
 *
 * Publica em MQTT o estado do barramento RS485 e de cada dispositivo Modbus
 * configurado (ver modbus_rs485_config.h e modbus_rs485_master.h). Topicos:
 *
 *   delinova/o/aquapulse/fazenda01/acude01/config-rs485
 *     {"status":bool,"bd":numero,"Par":"8N1"}
 *
 *   delinova/o/aquapulse/fazenda01/acude01/<identificador_mqtt>
 *     {"status":bool,"Slave-id":numero,"<campo_mqtt>":numero, ...}
 *     (status=false publica os campos numericos como 0, nunca string)
 *
 * Chamar Modbus_RS485_Mqtt_Iniciar() uma vez (depois de Modbus_RS485_Iniciar()
 * e de Mqtt_Kincony_Init()), e Modbus_RS485_Mqtt_Processar() a cada volta do
 * loop principal (se auto-limita internamente, igual aos outros _Processar()
 * do projeto).
 */

void Modbus_RS485_Mqtt_Iniciar(void);
void Modbus_RS485_Mqtt_Processar(void);

#endif
