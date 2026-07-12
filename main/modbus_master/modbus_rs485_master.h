#ifndef MODBUS_RS485_MASTER_H
#define MODBUS_RS485_MASTER_H

/*
 * modbus_rs485_master.h
 *
 * Criado por Eraldo Bispo - 23/06/2026
 *
 * Motor Modbus RTU master generico para a RS485 da KC868-A6. Roda numa task
 * FreeRTOS dedicada (independente da task main - WiFi/MQTT/API/controle dos
 * aeradores continuam funcionando mesmo se o Modbus falhar, e vice-versa).
 * Usa o componente esp-modbus (mbc_master_*) como motor de protocolo (CRC,
 * framing e timeout ja vem prontos dele) - aqui so fica o scheduler
 * round-robin sobre dispositivos/canais configurados (ver
 * modbus_rs485_config.h) e a conversao de registradores brutos pra valor de
 * engenharia.
 *
 */

#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"
#include "modbus_rs485_config.h"

// Sobe a configuracao (NVS ou padrao de fabrica), e se config.habilitado=true inicia a UART
// (mesmos pinos da RS485 fisica: UART2, TX=27, RX=14
esp_err_t Modbus_RS485_Iniciar(void);

bool Modbus_RS485_EstaHabilitado(void);

// Copia uma "foto" (snapshot) protegida por mutex de toda a configuracao + estado em runtime
// atual (valores dos canais, contadores, online/offline) para destino. Usado pelo modulo MQTT
// (modbus_rs485_mqtt.c) pra montar os payloads sem competir com a task do scheduler.
esp_err_t Modbus_RS485_ObterSnapshot(modbus_rs485_configuracao_t *destino);

#endif
