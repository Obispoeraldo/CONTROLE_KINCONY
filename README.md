# AquaPulse — Controlador de Aeradores (KC868-A6)

Firmware ESP32 para controle automatizado de aeradores de aquicultura via MQTT,
com painel web de configuração e leitura Modbus RTU RS485 de sensores de campo.

---

## Sumário

1. [Hardware](#hardware)
2. [Arquitetura do firmware](#arquitetura-do-firmware)
3. [Painel web](#painel-web)
4. [Tópicos MQTT](#tópicos-mqtt)
5. [Modbus RTU RS485 — tutorial de configuração](#modbus-rtu-rs485--tutorial-de-configuração)
6. [Compilação e gravação](#compilação-e-gravação)
7. [Configuração inicial (menuconfig)](#configuração-inicial-menuconfig)

---

## Hardware

| Item | Modelo |
|---|---|
| Controlador | KinCony KC868-A6 (ESP32) |
| Entradas digitais | 6 × optoacoplador |
| Saídas digitais | 6 × relé |
| Barramento RS485 | UART2, half-duplex, pino DE/RE integrado |
| Sensor de exemplo | Renkeer RS-LDO-N01-EX (O₂ dissolvido + temperatura) |

---

## Arquitetura do firmware

```
main/
├── main.c                          Ponto de entrada; inicializa módulos em ordem
├── Kconfig.projbuild               Defaults de fábrica (WiFi, broker, NTP, intervalo MQTT)
│
├── config_server/
│   ├── config_server_kincony.h/.c  NVS + painel web (WiFi/Broker/NTP/Intervalo)
│
├── wifi/
│   ├── wifi_kincony.h/.c           Conexão WiFi + AP de emergência
│
├── mqtt/
│   ├── mqtt_kincony.h/.c           Cliente MQTT (HiveMQ Cloud / qualquer broker)
│
├── ntp/
│   ├── ntp_kincony.h/.c            Cliente SNTP único do projeto; corrige o RTC físico a cada sincronização
│
├── rtc/
│   ├── rtc_ds1307.h/.c             RTC físico DS1307 (bateria) + 4 timers de horário para os aeradores
│
├── modbus_master/
│   ├── modbus_rs485_config.h/.c    Estruturas de configuração + persistência NVS
│   ├── modbus_rs485_master.h/.c    Task dedicada de leitura Modbus (FC03)
│   ├── modbus_rs485_mqtt.h/.c      Publicação dos valores lidos no broker MQTT
│   └── modbus_rs485_web.h/.c       Painel web /rs485 (editar barramento e dispositivos)
│
├── logica_controle/
│   └── logica_controle.h/.c        Máquina de estados dos aeradores (grupos/falhas/alarme)
│
├── entradas_kincony/
│   └── entradas_kincony.h/.c       Leitura das 6 entradas digitais
│
├── saidas_digitais_kincony/
│   └── saidas_digitais_kincony.h/.c Controle dos 6 relés
│
└── ota_github/
    └── ota_github.h/.c             Atualização OTA via GitHub Releases
```

### Fluxo de inicialização (`main.c`)

```
Entradas/Saídas (I2C)  →  RTC físico  →  NVS/Config  →  WiFi  →  NTP  →  HTTP  →  MQTT  →  Modbus  →  OTA  →  loop principal
```

**Serviço de tempo** (RTC + NTP, unificado): o RTC DS1307 fornece a hora já no boot,
antes de qualquer rede (se o chip estiver presente na placa — caso contrário o boot
segue normalmente e a hora fica pendente do NTP). Assim que o WiFi conecta, o NTP
(único cliente SNTP do projeto) sincroniza o relógio do sistema e, a cada
sincronização bem-sucedida, corrige também o RTC físico — que passa a manter a hora
correta durante quedas de energia/internet, graças à bateria.

O loop principal (200 ms/ciclo) executa:
- `Entradas_Kincony_Processar()` — lê entradas digitais
- `Logica_Controle_Processar()` — máquina de estados (partida/parada/falha/alarme)
- `RTC_DS1307_Processar()` — executa os 4 timers de horário dos aeradores (autolimitado a 1×/s)
- `Modbus_RS485_Mqtt_Processar()` — publica leituras Modbus no broker
- `Mqtt_Kincony_Processar()` — publicação periódica de monitoramento

A leitura Modbus corre em **task FreeRTOS dedicada** (não bloqueia o loop principal).

---

## Painel web

Acesse `http://<IP_DO_ESP>/` após o login (credenciais configuráveis em menuconfig).

| Rota | Método | Descrição |
|---|---|---|
| `/` | GET | **Status** — WiFi, MQTT, COM RS485, Data, Hora |
| `/configuracoes` | GET | **Configurações** — WiFi, Broker MQTT, NTP, Intervalo |
| `/salvar` | POST | Salva configurações e reinicia |
| `/salvar_intervalo_mqtt` | POST | Altera o intervalo MQTT **sem reiniciar** |
| `/rs485` | GET/POST | Configuração do barramento RS485 |
| `/rs485/dispositivo?indice=N` | GET/POST | Configuração de um dispositivo Modbus |
| `/login` | GET/POST | Autenticação |
| `/logout` | GET | Encerrar sessão |

**Intervalo de atualização MQTT** pode ser alterado de 1 a 180 segundos em tempo real,
sem reinicializar o controlador.

---

## Tópicos MQTT

Convenção de prefixos:
- `delinova/o/...` — **output**: publicações do controlador para o broker
- `delinova/i/...` — **input**: comandos enviados da aplicação para o controlador

Base do projeto: `delinova/o/aquapulse/fazenda01/acude01`

### Tópicos publicados pelo controlador

| Tópico | Retain | Conteúdo |
|---|---|---|
| `delinova/o/aquapulse/fazenda01/acude01/state` | Não | Estado completo (grupos, falhas, ts) |
| `delinova/o/aquapulse/fazenda01/acude01/status` | Não | `{"status":"online","fw":"...","IP":"..."}` |
| `delinova/o/aquapulse/fazenda01/acude01/config-rs485` | Não | `{"status":true,"bd":9600,"Par":"8N1"}` |
| `delinova/o/aquapulse/fazenda01/acude01/<id_mqtt>` | Não | Valores lidos do dispositivo Modbus |
| `delinova/o/aquapulse/fazenda01/acude01/status/rtc` | Não | Resposta dos comandos de RTC/timers (`rtc_get`, `timer_get`, `rtc_sync`, etc.) |

Exemplo de payload `state`:
```json
{
  "id": "aerador-01",
  "ts": 1720700000,
  "mode": "remoto",
  "online": true,
  "alarm": false,
  "groups": [
    {"g":1,"out":true,"fb":true,"fault":false,"src":"manual"},
    {"g":2,"out":false,"fb":false,"fault":false,"src":"manual"}
  ]
}
```

Exemplo de payload de dispositivo Modbus (sensor Renkeer, `id_mqtt = "renkeer01"`):
```json
{"status":true,"Slave-id":1,"sat":95.4,"O2":8.12,"temp":24.3}
```

### Tópico de comando (controlador escuta)

| Tópico | Payload JSON |
|---|---|
| `delinova/i/aquapulse/fazenda01/acude01/cmd` | `{"group":1,"action":"on"}` |

Campos de `action`: `"on"`, `"off"`, `"reset_faults"`, `"ota_enable"`, `"reset_esp"`.

O mesmo tópico `cmd` também aceita comandos de RTC/timers (interceptados antes do
parser acima — resposta publicada em `.../status/rtc`):
`{"action":"rtc_get"}`, `{"action":"rtc_sync"}`,
`{"action":"timer_set","id":1,"enabled":true,"group":1,"on":"06:00","off":"18:00"}`,
`{"action":"timer_get"}`, `{"action":"timer_enable","id":1}`,
`{"action":"timer_disable","id":1}`, `{"action":"timer_clear","id":1}`,
`{"action":"timer_clear_all"}`.
`group: 0` aplica o comando em todos os grupos simultaneamente.

---

## Modbus RTU RS485 — tutorial de configuração

### Visão geral

O controlador age como **Modbus RTU Master** (leitura apenas, FC03 — Read Holding Registers).
Até 4 dispositivos escravos podem ser configurados, cada um com até 4 canais independentes.

A configuração é feita pelo painel web em `/rs485` → dispositivo → canal, sem regravar o firmware.

### Parâmetros do barramento (`/rs485`)

| Campo | Descrição | Valor recomendado |
|---|---|---|
| Habilitado | Liga/desliga o motor Modbus | ✓ |
| Baud Rate | Velocidade da serial | 9600 |
| Paridade | N (nenhuma), E (par), O (ímpar) | N |
| Bits de dados | Normalmente 8 | 8 |
| Bits de parada | Normalmente 1 | 1 |
| Timeout de resposta (ms) | Aguarda resposta do escravo | 500 |
| Intervalo entre requisições (ms) | Pausa entre leituras de canais | 50 |
| Tentativas | Retentativas antes de marcar offline | 3 |

### Parâmetros de dispositivo

| Campo | Descrição |
|---|---|
| Habilitado | Inclui o dispositivo no ciclo de leitura |
| Nome | Nome livre para identificação |
| Identificador MQTT | Parte final do tópico MQTT de publicação |
| Endereço escravo | Endereço Modbus do dispositivo (1–247) |
| Timeout (ms) | 0 = usa o timeout do barramento |

### Parâmetros de canal

| Campo | Descrição |
|---|---|
| Habilitado | Inclui o canal na leitura |
| Nome | Nome livre |
| Endereço inicial | Endereço do primeiro registrador (decimal) |
| Quantidade de registradores | 1 para uint16/int16, 2 para uint32/int32/float32 |
| Tipo de dado | uint16, int16, uint32, int32, float32 |
| Ordem de palavra | ABCD / CDAB / BADC / DCBA (relevante para 32 bits) |
| Escala | Multiplicador aplicado ao valor bruto |
| Deslocamento | Somado após a escala |
| Casas decimais | Precisão para publicação MQTT |
| Unidade | Texto livre (ex: "mg/L", "°C") |
| Campo MQTT | Nome do campo no JSON publicado |
| Ciclo de leitura (ms) | Intervalo mínimo entre leituras deste canal |

### Exemplo real: Renkeer RS-LDO-N01-EX

O RS-LDO-N01-EX mede **saturação de O₂ (%)**, **O₂ dissolvido (mg/L)** e **temperatura (°C)**
via Modbus RTU. Registros confirmados no datasheet v1.9 (float32, formato big-endian ABCD):

| Grandeza | Endereço inicial (hex) | Registradores | Tipo | Ordem |
|---|---|---|---|---|
| Saturação (%) | 0x0000 | 2 | float32 | ABCD |
| O₂ dissolvido (mg/L) | 0x0002 | 2 | float32 | ABCD |
| Temperatura (°C) | 0x0004 | 2 | float32 | ABCD |

**Configuração de barramento para este sensor:**
- Baud Rate: **9600** (padrão de fábrica 4800 — ajustar no sensor antes de conectar)
- Paridade: N, 8 bits, 1 stop

**Configuração do dispositivo:**
- Endereço escravo: 1 (padrão de fábrica; ajustável via software proprietário Renkeer)
- Identificador MQTT: `renkeer01`

**Configuração dos canais:**

| Canal | Nome | End. inicial (dec) | Qtde reg | Tipo | Ordem | Escala | Casas | Unidade | Campo MQTT |
|---|---|---|---|---|---|---|---|---|---|
| 1 | Saturação O₂ | 0 | 2 | float32 | ABCD | 1.0 | 1 | % | sat |
| 2 | O₂ Dissolvido | 2 | 2 | float32 | ABCD | 1.0 | 2 | mg/L | O2 |
| 3 | Temperatura | 4 | 2 | float32 | ABCD | 1.0 | 1 | °C | temp |

**Payload MQTT resultante** (tópico `delinova/o/aquapulse/fazenda01/acude01/renkeer01`):
```json
{"status":true,"Slave-id":1,"sat":95.4,"O2":8.12,"temp":24.3}
```

> **Atenção:** nunca invente endereços de registrador. Use apenas os confirmados no
> datasheet do fabricante. Endereços errados retornam exceção Modbus (código 02)
> e o dispositivo é marcado offline após as tentativas esgotadas.

---

## Compilação e gravação

**Pré-requisitos:** ESP-IDF v6.0.1, VS Code com extensão ESP-IDF.

```bash
# Configurar target
idf.py set-target esp32

# Ajustar credenciais iniciais (WiFi, broker, login)
idf.py menuconfig   # menu: Kincony Configuration

# Compilar
idf.py build

# Gravar e monitorar
idf.py flash monitor
```

O IP do controlador aparece no monitor serial após a mensagem `WiFi conectado`.

---

## Configuração inicial (menuconfig)

Todos os valores abaixo são **padrões de fábrica**: valem apenas na primeira gravação.
Após o primeiro boot, edite pelo painel web (`/configuracoes`) — os valores são salvos em NVS
e o menuconfig é ignorado nas gravações seguintes.

| Chave | Descrição | Default |
|---|---|---|
| `KINCONY_WIFI_SSID` | Nome da rede WiFi | — |
| `KINCONY_WIFI_PASSWORD` | Senha da rede WiFi | — |
| `KINCONY_MQTT_BROKER_URI` | URI do broker MQTT | HiveMQ Cloud |
| `KINCONY_MQTT_USER` | Usuário do broker | `administrador` |
| `KINCONY_MQTT_PASSWORD` | Senha do broker | — |
| `KINCONY_HTTP_USER` | Login do painel web | `aquapulse` |
| `KINCONY_HTTP_PASSWORD` | Senha do painel web | `aquapulse2026` |
| `KINCONY_NTP_SERVIDOR` | Servidor NTP | `pool.ntp.org` |
| `KINCONY_NTP_FUSO_HORAS` | Fuso horário (UTC offset) | `-3` (Brasília) |
| `KINCONY_MQTT_INTERVALO_S` | Intervalo de publicação MQTT (s) | `5` |

---

*Criado por Eraldo Bispo — 11/07/2026*
