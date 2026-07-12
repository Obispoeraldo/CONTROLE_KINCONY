#ifndef OTA_GITHUB_H
#define OTA_GITHUB_H

#include "esp_err.h"

// Editado por Eraldo Bispo e Daniel Montanher - integracao - v1.0.11 (Daniel, origin/main) +
// desenvolvimento paralelo de Eraldo (Modbus RTU master, NTP, WiFi/AP de emergencia, painel web)
// integrados nesta branch (integration/eraldo-daniel). SemVer pre-release: nao e a versao
// estavel definitiva ate a validacao em hardware descrita em
// docs/RELATORIO_INTEGRACAO_ERALDO_DANIEL.md.
#define FIRMWARE_VERSION "1.1.0-integration.1"

esp_err_t ota_github_check_update(void);

// Criado por Eraldo Bispo — inicia uma task FreeRTOS propria, com pilha dedicada, que chama
// ota_github_check_update() periodicamente. Antes essa checagem rodava direto na task "main"
// (TLS + HTTP + parse do JSON), causando estouro de pilha (vApplicationStackOverflowHook).
void Ota_Github_IniciarTask(void);

#endif 