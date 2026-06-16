#ifndef OTA_GITHUB_H
#define OTA_GITHUB_H

#include "esp_err.h"

#define FIRMWARE_VERSION "1.0.8"

esp_err_t ota_github_check_update(void);
void verifica_atualizacao(void);

#endif 