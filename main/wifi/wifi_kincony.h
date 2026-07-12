#ifndef WIFI_KINCONY_H
#define WIFI_KINCONY_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

// Editado por Eraldo Bispo - 11/07/2026 - sinaliza falha inicial ao main.c (para acionar
// backup WiFi) apos este numero de falhas rapidas. Apos isso, o modulo CONTINUA tentando
// indefinidamente com backoff exponencial — nunca desiste. Ver wifi_kincony.c.
#define WIFI_KINCONY_TENTATIVAS_FALHA_INICIAL  3

esp_err_t Wifi_Kincony_Init(const char *ssid, const char *senha);

// Criado por Eraldo Bispo — bloqueia ate conectar ou falhar definitivamente (WIFI_KINCONY_TENTATIVAS_MAX esgotadas).
// Retorna true se conectou dentro do timeout, false se falhou ou estourou o tempo.
// Usado no main.c para reverter automaticamente para o WiFi anterior se uma troca pelo painel web falhar.
bool Wifi_Kincony_EsperarResultado(uint32_t timeout_ms);

bool Wifi_Kincony_IsConectado(void);
bool Wifi_Kincony_IsFalha(void);

// Criado por Eraldo Bispo - 23/06/2026 - copia o IP atual da interface STA (ex: "192.168.1.3")
// para destino. Se nao estiver conectado, escreve string vazia (nunca "0.0.0.0"), para o
// chamador (status MQTT) saber que nao deve publicar IP nesse caso.
void Wifi_Kincony_GetIpAtual(char *destino, size_t tamanho);

uint8_t Wifi_Kincony_GetTentativas(void);

esp_err_t Wifi_Kincony_Reconectar(void);
esp_err_t Wifi_Kincony_Desconectar(void);

// Criado por Eraldo Bispo — modo de emergencia: liga um Access Point proprio do ESP32 (alem do
// STA, que continua tentando reconectar em segundo plano) para quando nenhuma rede WiFi conhecida
// (nem a atual nem o backup) for encontrada. Conecte na rede "Kincony-Config" e acesse
// http://192.168.4.1 para corrigir o WiFi salvo, sem precisar de cabo USB.
esp_err_t Wifi_Kincony_IniciarModoEmergenciaAP(void);

#endif