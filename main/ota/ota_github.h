#ifndef OTA_GITHUB_H
#define OTA_GITHUB_H

#include "esp_err.h"

#define FIRMWARE_VERSION "1.0.11"

esp_err_t ota_github_check_update(void);

// Criado por Eraldo Bispo — inicia uma task FreeRTOS propria, com pilha dedicada, que chama
// ota_github_check_update() periodicamente. Antes essa checagem rodava direto na task "main"
// (TLS + HTTP + parse do JSON), causando estouro de pilha (vApplicationStackOverflowHook).
void Ota_Github_IniciarTask(void);

#endif 