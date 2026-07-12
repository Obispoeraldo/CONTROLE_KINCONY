# Relatório de Integração — AquaPulse (Eraldo Bispo × Daniel Montanher)

Data da análise: 2026-07-12
Branch de integração: `integration/eraldo-daniel` (criada a partir de `origin/main`, worktree isolado em `C:\Users\erald\CONTROLE_KINCONY_integration`)

## 1. Identificação das versões

| Item | Valor |
|---|---|
| Branch de Daniel (referência oficial publicada) | `origin/main` (remoto `origin` = `https://github.com/dnx98/CONTROLE_KINCONY.git`) |
| Commit-base de Daniel | `98f683b` — "adicionado controle dos timmers atraves do rtc 24/06/26 as 23:33" |
| Branch de Eraldo (snapshot preservado) | `painel-config-wifi` (local + `origin/painel-config-wifi` + `meufork/painel-config-wifi`) |
| Commit-base de Eraldo | `6a4b9af` — inclui o commit de preservação `feat(modbus-master,ntp): ...` criado nesta auditoria a partir do working directory não commitado |
| Ancestral comum (`git merge-base`) | `afa93fb` — "Adiciona painel web de configuracao de WiFi/broker com protecao contra senha WiFi invalida" |
| ESP-IDF | v6.0.1 (`C:\esp\v6.0.1\esp-idf`) |
| Target | `esp32` (KinCony KC868-A6) |
| Firmware antes da integração | `1.0.11` (`origin/main`, `main/ota/ota_github.h`) |
| Firmware desta integração | `1.1.0-integration.1` |
| Toolchain | xtensa-esp-elf GCC 15.2.0, Python 3.12.10 |

Histórico relevante: `afa93fb` (painel web de Eraldo) foi mesclado em `origin/main` pelo PR #1 (`b07ddf0`). A partir daí os dois desenvolvimentos divergiram: Daniel adicionou `c0cad19`/`98f683b` (RTC DS1307 + timers) direto em `origin/main`; Eraldo continuou em `painel-config-wifi` com `c03d2ee`, `c54f3be`, `67ff57e`, `3aa09bb`, mais um volume grande de alterações que permaneciam **não commitadas** no working directory no início desta auditoria (Modbus RTU master, NTP, ampliações de config_server/MQTT/WiFi). Essas alterações foram preservadas no commit `6a4b9af` antes de qualquer merge (ver seção 9).

```
git log --left-right --cherry-pick --oneline painel-config-wifi...origin/main
> 98f683b adicionado controle dos timmers atraves do rtc 24/06/26 as 23:33
> c0cad19 adicionado rtc
< 3aa09bb Alarme na saida 06, comando imediato e correcao da URI do broker MQTT
< 67ff57e Move OTA para task propria e credenciais MQTT configuraveis pelo painel web
< c54f3be Adiciona Access Point de emergencia quando nenhum WiFi conhecido e encontrado
< c03d2ee Corrige nomes de componentes para IDF v6 (esp-mqtt, cjson) e tabela de particoes
> b07ddf0 Merge pull request #1 from Obispoeraldo/painel-config-wifi
```

## 2. Alterações realizadas por Daniel (`origin/main`, desde `afa93fb`)

| Arquivo | Resumo |
|---|---|
| `main/rtc/rtc_ds1307.c/.h` (novo, 1252+84 linhas) | Driver DS1307 (I2C, bateria), 4 timers de horário persistentes em NVS (`namespace "rtc_timers"`), comandos MQTT `timer_set/get/enable/disable/clear/clear_all`, `rtc_get`, `rtc_sync` |
| `main/mqtt/mqtt_kincony.c/.h` | Removeu parser de comando texto legado (morto/não usado), padronizou `publicar_ack_comando()` para `MQTT_TOPIC_STATE` com schema `{type,group,action,result,remote}`, integrou RTC (sync ao conectar, comandos RTC, `RTC_DS1307_RegistrarComandoManual`), tópico `MQTT_TOPIC_STATUS_RTC` |
| `main/main.c` | `RTC_DS1307_Iniciar()` no boot, `RTC_DS1307_Processar()` no loop |
| `main/wifi/wifi_kincony.c` | 1 chamada a `RTC_DS1307_AtualizarHorarioInternet()` após iniciar STA |
| `main/CMakeLists.txt` | Registra `rtc/rtc_ds1307.c` e include dir `rtc` |
| `.vscode/settings.json`, `.vscode/c_cpp_properties.json`, `dependencies.lock` | Configuração local da máquina de Daniel (`C:\Users\danmo\...`), porta serial `COM3` |

Dependências introduzidas: nenhuma nova (usa `driver/i2c.h`, `cJSON`, já presentes).

## 3. Alterações realizadas por Eraldo (branch `painel-config-wifi`, commit local `6a4b9af`, desde `afa93fb`)

| Arquivo | Resumo |
|---|---|
| `main/modbus_master/*` (novo, ~2000 linhas) | Motor Modbus RTU **master** genérico e configurável (N dispositivos × canais), scheduler round-robin sobre `esp-modbus`, ponte MQTT, página web "RS485 / Modbus RTU" |
| `main/ntp/ntp_kincony.c/.h` (novo) | Cliente SNTP (`esp_netif_sntp`), fuso fixo, `Ntp_Kincony_ObterDataHoraFormatada()` |
| `main/wifi/wifi_kincony.c/.h` | Backoff exponencial não bloqueante (`esp_timer`), IP atual exposto, Access Point de emergência (`Wifi_Kincony_IniciarModoEmergenciaAP`) |
| `main/mqtt/mqtt_kincony.c/.h` | Credenciais configuráveis (removeu usuário/senha fixos no código), intervalo de publicação configurável em runtime, `Mqtt_Kincony_PublicarRetido`, callback de reconexão, LWT com IP, separação de tópicos `delinova/i/...` (comando) / `delinova/o/...` (estado) |
| `main/config_server/*` | Painel web (WiFi/broker/NTP/intervalo MQTT), NVS namespace `kincony_cfg`, backup/restauração de WiFi, utilitários HTTP compartilhados |
| `main/logica_controle/*` | Alarme geral na saída 06 (`Logica_Controle_AtualizarAlarme`) por falha geral/MQTT/WiFi |
| `main/ota/ota_github.*` | OTA movido para task própria (pilha dedicada) |
| `main/modbus_rtu_slave/*` | Ajustes de nomes de componentes para IDF v6 |
| `main/CMakeLists.txt`, `Kconfig.projbuild`, `sdkconfig.defaults`, `idf_component.yml` | Registro dos novos módulos, defaults de fábrica (NTP, intervalo MQTT, credenciais) |
| `README.md` (novo) | Documentação completa do firmware |

Dependências introduzidas: `esp_netif` (REQUIRES, para `esp_netif_sntp`), correção de nomes gerenciados `esp-mqtt`/`cjson` (IDF v6).

## 4. Compatibilidade entre os desenvolvimentos — por área

| Área | Classificação | Observação |
|---|---|---|
| Máquina de estados dos aeradores (`logica_controle`) | Compatível | Nenhum dos dois alterou o núcleo da máquina de estados; Daniel apenas consome `Logica_Controle_SetComandoGrupo`/lê feedback via RTC (timers) e MQTT (comandos manuais), mesma API que já existia |
| Entradas/saídas digitais | Compatível | Sem alteração de nenhum dos dois lados |
| Modbus RTU (slave legado × master novo) | Compatível com ajustes | `ModbusRTU_Slave_Init()` nunca é chamado em nenhuma das duas branches (já morto antes da integração); `Modbus_RS485_Iniciar()` (Eraldo) é o único ativo, na mesma UART/RS485 física. Sem conflito em runtime, mas o módulo slave morto deveria ser removido futuramente |
| Cadastro de dispositivos/registradores Modbus | Compatível | Exclusivo de Eraldo, sem equivalente em Daniel |
| **Serviço de tempo (RTC × NTP)** | **Conflito estrutural — resolvido nesta integração** | Ver seção 5 |
| Comunicação MQTT — tópicos/ack | Compatível com ajustes | Ver seção 6 |
| Reconexão WiFi | Compatível | Só Eraldo alterou; Daniel não tocou no arquivo (exceto a 1 linha de RTC, removida — seção 5) |
| Reconexão MQTT | Compatível | Mesmo mecanismo (`esp_mqtt` autoreconnect); LWT/retain ajustados por Eraldo, mantidos |
| OTA | Compatível | Só Eraldo alterou (task própria); Daniel não tocou |
| Interface Web | Compatível | Exclusiva de Eraldo; Daniel não implementou UI para RTC/timers (pendência, ver seção 12) |
| Persistência NVS | Compatível | Namespaces distintos e não sobrepostos: `kincony_cfg` (config_server), `modbus_rs485` (Modbus master), `rtc_timers` (RTC). Ver seção 7 |
| Configuração RS485 | Compatível | Só Eraldo implementou (RTU master); Daniel não usa RS485 |
| Periodicidade das publicações | Compatível com ajustes | Unificado: `Mqtt_Kincony_GetIntervaloMs()` é a fonte única, inclusive para `modbus_rs485_mqtt.c` |
| Watchdog | Compatível | Nenhum dos dois alterou o watchdog padrão do IDF |
| Timers/tasks FreeRTOS | Compatível com ajustes | Ver tabela da seção 8 |
| Filas | Compatível | Nenhum módulo introduz filas próprias que se sobreponham |
| Mutexes | Compatível | RTC (`s_mutex`), Modbus master (`mutex_estado`) — escopos disjuntos, sem lock aninhado entre eles |
| Memória (RAM/Flash) | Compatível — ver seção 10 | Build validado, folga confortável em Flash e DRAM |
| `sdkconfig`/`Kconfig`/CMake | Compatível com ajustes | `sdkconfig` não é versionado (gitignored); `Kconfig.projbuild` e `CMakeLists.txt` unificados manualmente (seção 9) |
| Partições | Compatível | Tabela de partições (`ota_0`/`ota_1`, 1800K cada) não foi alterada por nenhum dos dois lados |
| Certificados/credenciais | **Crítico — ver seção 11** | Credencial MQTT hardcoded (`administrador`/`Administrador2026`) exposta no histórico do Git |
| Documentação | Compatível com ajustes | README novo (Eraldo) não documentava RTC/timers de Daniel; complementado nesta integração |

## 5. Serviço de tempo — RTC (Daniel) × NTP (Eraldo)

### Comportamento antes da integração
- **Daniel**: `RTC_DS1307_Iniciar()` lê o chip DS1307 no boot e aplica ao relógio do sistema. `RTC_DS1307_AtualizarHorarioInternet()` mantinha um **cliente SNTP próprio** (API legada `esp_sntp.h`: `esp_sntp_init/setservername/stop`), acionado via task dedicada (`RTC_DS1307_SolicitarSincronizacaoInternet()`) a cada conexão MQTT e sob demanda (`rtc_sync`). Além disso, `Wifi_Kincony_Init()` (em `origin/main`) chamava `RTC_DS1307_AtualizarHorarioInternet()` de forma **bloqueante** logo após `esp_wifi_start()` — antes mesmo da conexão STA ser confirmada. `ESP_ERROR_CHECK(RTC_DS1307_Iniciar())` em `main.c` **derruba o boot** (reboot loop) se o chip DS1307 não responder no I2C.
- **Eraldo**: `Ntp_Kincony_Iniciar()` usa a API moderna `esp_netif_sntp` (`esp_netif_sntp_init/start`), não bloqueante, chamada uma única vez após o WiFi conectar. O comentário original do módulo afirma explicitamente **"o KC868-A6 nao tem RTC com bateria no projeto hoje"** — contradizendo a premissa de hardware de Daniel.

### Conflito identificado
1. **Dois clientes SNTP concorrentes** controlando o mesmo módulo SNTP global do lwIP (API legada `esp_sntp.h` de Daniel + API moderna `esp_netif_sntp` de Eraldo) — reinicialização concorrente do mesmo recurso, não suportado pela Espressif.
2. **Divergência de premissa de hardware**: os comentários dos dois desenvolvedores se contradizem sobre a existência física do RTC DS1307 na placa em uso. Isso **não pôde ser resolvido por código** — é uma questão de hardware real.
3. `ESP_ERROR_CHECK(RTC_DS1307_Iniciar())` cria risco de **boot-loop crítico** caso o chip realmente não exista na variante de placa do Eraldo.
4. Chamada bloqueante de sincronização dentro de `Wifi_Kincony_Init()`, antes da rede estar pronta.

### Solução unificada implementada (Boot → RTC → WiFi → NTP → corrige RTC)
Conforme o fluxo prescrito:
```
Boot → RTC_DS1307_Iniciar() fornece a hora (se o chip responder) → boot continua
     → WiFi conecta → Ntp_Kincony_Iniciar() (ÚNICO cliente SNTP do projeto)
     → NTP sincroniza → sync_cb (ntp_sync_callback) dispara
     → RTC_DS1307_NotificarSincronizacaoNtp(tv) grava a hora corrigida no DS1307 (se presente)
     → RTC continua fornecendo a hora em quedas de energia/internet (bateria)
```

Mudanças de código:
- `rtc_ds1307.c`: removidas `RTC_DS1307_AtualizarHorarioInternet()`, `RTC_DS1307_SolicitarSincronizacaoInternet()` e a task `rtc_ntp_task` (cliente SNTP próprio). Adicionada `RTC_DS1307_NotificarSincronizacaoNtp(const struct timeval *tv)`, chamada pelo `sync_cb` único.
- `ntp_kincony.c`: `Ntp_Kincony_Iniciar()` registra `sync_cb = ntp_sync_callback`, que chama `RTC_DS1307_NotificarSincronizacaoNtp()`. Nova `Ntp_Kincony_ForcarResincronizacao()` (`esp_netif_sntp_start()`) usada pelo comando MQTT `rtc_sync`, em vez de abrir um segundo cliente.
- `RTC_DS1307_Iniciar()` **não falha mais o boot** se o chip não responder: loga aviso, marca `s_rtc_disponivel = false` e retorna `ESP_OK`. `RTC_DS1307_Processar()` sai imediatamente nesse caso (evita flood de erros I2C a cada segundo). `main.c` mantém `ESP_ERROR_CHECK(RTC_DS1307_Iniciar())` sem risco, pois a função não retorna mais erro nesse cenário (só falha por falta de memória para o mutex, uma condição já fatal por si).
- `Wifi_Kincony_Init()` (versão de Eraldo, adotada) **não** recebeu a chamada de sincronização de Daniel — o NTP agora roda de forma desacoplada, disparado pelo próprio `main.c` após o resultado do WiFi.
- Prioridade de fontes de hora, conforme solicitado: 1) NTP válido corrige o relógio do sistema e o RTC; 2) RTC físico válido mantém a hora sem internet; 3) hora inválida (`"Sincronizando..."` / `"clock_source":"invalid"`) quando nenhum dos dois está disponível.
- Nova API pública: `RTC_DS1307_EstaDisponivel()` (status do chip) e `RTC_DS1307_NotificarSincronizacaoNtp()`.

### Risco residual — **requer validação em hardware**
Não foi possível confirmar, sem acesso físico à placa, se o DS1307 está de fato soldado na variante da KC868-A6 usada por Eraldo. A integração foi projetada para ser segura em **ambos os casos** (com ou sem o chip), mas o comportamento de campo (timers de horário funcionando ou não) só pode ser confirmado com o hardware em mãos. **Severidade: Alta. Ação: testar `RTC_DS1307_EstaDisponivel()` / logs de boot no primeiro flash.**

## 6. Comunicação MQTT

| Item | Antes (Daniel) | Antes (Eraldo) | Solução unificada |
|---|---|---|---|
| Tópicos | `MQTT_BASE_TOPIC` único (`delinova/o/...`), `MQTT_TOPIC_CMD` também sob `/o/` | `MQTT_BASE_TOPIC_OUT` (`/o/`) e `MQTT_BASE_TOPIC_IN` (`/i/`), `CMD` sob `/i/` | Adotada a separação de Eraldo: `STATE`/`STATUS`/`STATUS_RTC` sob `/o/`; `CMD` sob `/i/` |
| Credenciais do broker | Fixas no código (`"administrador"`/`"Administrador2026"`) | Configuráveis via NVS/painel web | Adotado o de Eraldo — ver seção 11 (rotação de senha recomendada) |
| Ack de comando | Já refatorado por Daniel para `MQTT_TOPIC_STATE`, schema `{type,group,action,result,remote}` (substituiu o tópico solto `"kincony/comando/ack"` que não seguia o padrão `delinova/...`) | Não alterado (ainda usava o tópico antigo) | Mantida a versão de Daniel (correção legítima de inconsistência pré-existente) |
| Retain | Publicação "online" com retain (`MQTT_RETAIN_STATUS`) | Publicação "online" **sem** retain (`MQTT_RETAIN_MONITOR`); nova `Mqtt_Kincony_PublicarRetido()` para tópicos que precisam de retain (ex. config RS485) | Adotado o de Eraldo: telemetria/status "online" sem retain; **LWT "offline" continua com retain = true** (ambos concordam) |
| Intervalo de publicação | Fixo (`MQTT_PUBLICACAO_MONITORAMENTO_MS = 1000`) | Configurável em runtime via NVS (`Mqtt_Kincony_SetIntervaloMs`) | Adotado o de Eraldo |
| Integração RTC | `RTC_DS1307_ProcessarComandoMQTT()` interceptado antes do parser padrão (`timer_*`, `rtc_get`, `rtc_sync`) | N/A | Mantido; `rtc_sync` agora aciona `Ntp_Kincony_ForcarResincronizacao()` (seção 5) |
| Comandos nunca retidos | N/A | `Mqtt_Kincony_Publicar()` (usada por ack e comandos) permanece `MQTT_RETAIN_MONITOR=0` | Confirmado — nenhum comando/ack é publicado com retain |

Fonte única de publicação de estado: `Mqtt_Kincony_PublicarMonitoramento()` (chamada por `Mqtt_Kincony_Processar()` no intervalo configurado e a cada comando recebido) — inalterada em relação a antes, sem duplicação.

## 7. Persistência (NVS)

| Namespace | Módulo | Chaves | Observação |
|---|---|---|---|
| `kincony_cfg` | `config_server_kincony.c` (Eraldo) | WiFi, broker, credenciais MQTT, NTP, intervalo MQTT, backup WiFi | Sem alteração de schema nesta integração |
| `modbus_rs485` | `modbus_rs485_config.c` (Eraldo) | 1 chave de barramento + 1 por dispositivo | Exclusivo, sem equivalente em Daniel |
| `rtc_timers` | `rtc_ds1307.c` (Daniel) | `config` (blob com magic `0x52544354` + versão `1`) | Exclusivo, sem equivalente em Eraldo. Já tinha proteção de versão/magic antes desta integração — nenhuma migração foi necessária |

**Nenhum namespace, chave ou tipo foi alterado ou removido** nesta integração. Não há risco de perda de configuração já persistida em equipamentos que já rodavam qualquer uma das duas versões anteriores — cada módulo mantém seu próprio namespace intocado.

## 8. Tasks, timers e recursos compartilhados

| Timer/Task | Origem | Período | Prioridade/Stack | Responsabilidade | Recurso compartilhado | Decisão de integração |
|---|---|---|---|---|---|---|
| `app_main` (loop principal) | Ambos (pré-existente) | 200 ms (valor de Eraldo, reduzido de 5000 ms) | Task principal | Entradas, lógica de controle, alarme, Modbus MQTT publish, MQTT processar, RTC processar | I2C (entradas/saídas/RTC) | Mantido 200 ms de Eraldo; `RTC_DS1307_Processar()` de Daniel inserido no loop (autolimitado a 1×/s internamente) |
| `modbus_rs485_master` | Eraldo | Round-robin configurável por dispositivo | Task dedicada | Polling Modbus RTU (UART2) | UART2, `mutex_estado` | Mantida como task própria, independente do loop principal |
| `rtc_ntp_sync` (task de Daniel) | Daniel | Sob demanda | 4096 stack, prio 5 | Cliente SNTP próprio | Cliente SNTP global | **Removida** — substituída pelo `sync_cb` do cliente SNTP único (seção 5) |
| SNTP (`esp_netif_sntp`) | Eraldo | Poll interno da lib | — | Sincronização de hora | Cliente SNTP global | Único cliente mantido; Daniel se conecta via callback |
| OTA (`Ota_Github_IniciarTask`) | Eraldo | Task própria, 10240 bytes stack | Dedicada | Checagem de atualização no GitHub | Nenhum | Mantida; Daniel não tinha equivalente (chamava direto no loop antes de Eraldo mover) |
| WiFi backoff (`esp_timer` `wifi_backoff`) | Eraldo | Exponencial 1s–30s | esp_timer (não task) | Reconexão WiFi não bloqueante | Nenhum | Mantida; Daniel não alterou reconexão WiFi |
| `Mqtt_Kincony_Processar` | Ambos (pré-existente) | Intervalo configurável (padrão 5000 ms) | No loop principal | Publicação periódica de monitoramento | Cliente MQTT | Unificado — fonte única do intervalo (`Config_Server_Kincony_GetMqttIntervaloMs`) |

Nenhum timer duplicado permanece após a integração (o único caso de sobreposição real — dois clientes SNTP — foi eliminado na seção 5).

## 9. Estratégia de integração executada

1. `origin/main` (`98f683b`) foi usado como base: `git branch -f integration/eraldo-daniel origin/main` (confirmado antes, via leitura, que a branch não tinha nenhum commit exclusivo que não estivesse preservado em `painel-config-wifi`/remotos).
2. Checkout em **worktree isolado** (`C:\Users\erald\CONTROLE_KINCONY_integration`) para não tocar o working directory principal do usuário (que tinha uma alteração local não commitada em `.vscode/settings.json`, preservada intacta).
3. Arquivos exclusivos de Eraldo (sem overlap com Daniel) trazidos integralmente via `git checkout painel-config-wifi -- <caminho>`: `README.md`, `main/modbus_master/*`, `main/ntp/*`, `main/config_server/*`, `main/logica_controle/*`, `main/modbus_rtu_slave/*`, `main/ota/*`, `main/wifi/*`, `main/Kconfig.projbuild`, `sdkconfig.defaults`, `main/idf_component.yml`.
4. Arquivos com sobreposição real resolvidos manualmente, por comportamento funcional (não `ours`/`theirs`): `main/CMakeLists.txt`, `main/main.c`, `main/mqtt/mqtt_kincony.c/.h`, `main/rtc/rtc_ds1307.c/.h`, `main/ntp/ntp_kincony.c/.h` — decisões documentadas nas seções 5 e 6.
5. `dependencies.lock` regenerado automaticamente pelo `idf.py` (formato de lock v2.0.0 → v3.0.0, novos componentes resolvidos).
6. `.vscode/settings.json` e `.vscode/c_cpp_properties.json` **não foram alterados** — permanecem com os caminhos locais da máquina de Daniel (`C:\Users\danmo\...`, porta `COM3`). Ver seção 11 (item de configuração particular).

### Conflitos resolvidos (detalhe por trecho)

| Arquivo | Trecho | Implementação de Daniel | Implementação de Eraldo | Decisão | Justificativa |
|---|---|---|---|---|---|
| `mqtt_kincony.h` | Base de tópicos | `MQTT_BASE_TOPIC` único (`/o/`) | `MQTT_BASE_TOPIC_OUT`/`_IN` | Eraldo | Requisito explícito do processo de integração (`delinova/i/` para comando, `/o/` para estado); `MQTT_TOPIC_STATUS_RTC` de Daniel preservado sob `/o/` |
| `mqtt_kincony.c` | `Mqtt_Kincony_Init()` | 1 parâmetro, credenciais fixas | 3 parâmetros, credenciais NVS | Eraldo | Remove credencial exposta no código-fonte (seção 11) |
| `mqtt_kincony.c` | Retain do status "online" | `MQTT_RETAIN_STATUS` (retido) | `MQTT_RETAIN_MONITOR` (não retido) | Eraldo | Assinantes não devem receber um "online" antigo como se fosse atual; LWT "offline" continua retido em ambas as versões |
| `mqtt_kincony.c` | `publicar_ack_comando` / tópico | Refatorado para `MQTT_TOPIC_STATE`, schema novo | Não tocado (tópico antigo `"kincony/comando/ack"`) | Daniel | Correção legítima de inconsistência: o tópico antigo não seguia o namespace `delinova/...` usado em todo o resto do projeto |
| `rtc_ds1307.c`/`ntp_kincony.c` | Cliente SNTP | Cliente próprio (`esp_sntp.h`, API legada) | Cliente próprio (`esp_netif_sntp`, API moderna) | Unificado (nenhum dos dois sozinho) | Dois clientes SNTP concorrentes; ver seção 5 para o novo design cooperativo |
| `main.c` | `ESP_ERROR_CHECK(RTC_DS1307_Iniciar())` | Falha o boot se DS1307 ausente | N/A (Eraldo não tinha RTC) | Mantida a chamada, mas `RTC_DS1307_Iniciar()` alterado para não retornar erro nesse caso | Evita boot-loop em placas sem o chip físico (ver seção 5) |
| `CMakeLists.txt` | `SRCS`/`INCLUDE_DIRS`/`REQUIRES` | Adiciona `rtc/` | Adiciona `modbus_master/`, `ntp/`, `esp_netif` | União de ambos | Sem conflito real — apenas soma de listas |

## 10. Build

```
idf.py set-target esp32
idf.py build
```

| Item | Resultado |
|---|---|
| Resultado | **Sucesso** (1080/1080 alvos, `CONTROLE_KINCONY.bin` gerado) |
| ESP-IDF | v6.0.1 |
| Target | esp32 |
| Tamanho da imagem | 1.233.660 bytes |
| Flash Code (.text) | 797.462 bytes |
| Flash Data (.rodata etc.) | 325.364 bytes |
| IRAM | 92.963 / 131.072 bytes (70,93%), 38.109 livres |
| DRAM | 42.671 / 180.736 bytes (23,61%), 138.065 livres |
| Partição de app (`ota_0`/`ota_1`, 1.800K cada) | 33% livre (609.168 bytes) por slot OTA |
| Bootloader | 9% livre |
| Erros | 0 |
| Warnings no código próprio (`main/`) | 0 |
| Warnings de dependências | 9 ocorrências — todas o mesmo aviso: depreciação do driver legado `driver/i2c.h` (`entradas_kincony.c`, `saidas_digitais_kincony.c`, `rtc_ds1307.c`), **END-OF-LIFE no IDF v6.0, remoção prevista no IDF v7.0**. Pré-existente (já usado por `entradas`/`saidas` antes desta integração); recomenda-se migrar para `driver/i2c_master.h` num próximo ciclo, fora do escopo desta integração |

Ambiente: Python real do sistema não estava associado a `python.exe`/`python3.exe` (apenas o alias da Microsoft Store); localizado em `C:\Users\erald\AppData\Local\Programs\Python\Python312\python.exe` (instalado via winget, `Python.Python.3.12`) e antepesto ao `PATH` da sessão de build. ESP-IDF tools/venv instalados via `install.bat esp32` (não existiam para este usuário do Windows — ambiente aparentemente configurado originalmente na máquina do Daniel, `C:\Users\danmo\...`).

## 11. Itens que podem impactar o funcionamento

| Item | Origem | Impacto | Severidade | Evidência | Recomendação |
|---|---|---|---|---|---|
| Credencial MQTT hardcoded (`"administrador"`/`"Administrador2026"`) presente no histórico do Git (commit anterior a `afa93fb`, ainda em `origin/main` até esta integração) | Pré-existente (ambos os remotos, `origin` e `meufork`, já têm esse histórico publicado) | Credencial do broker exposta publicamente no GitHub | **Crítica** | `git diff afa93fb painel-config-wifi -- main/mqtt/mqtt_kincony.c` (linhas removidas por Eraldo) | Rotacionar a senha no broker (HiveMQ Cloud) imediatamente; a credencial não pode ser removida do histórico Git sem reescrever o histórico (fora do escopo desta integração) |
| RTC DS1307: `ESP_ERROR_CHECK` sobre chip físico cuja presença é contestada entre os dois desenvolvedores | Daniel × Eraldo (contradição documentada nos comentários de cada um) | Boot-loop crítico se o chip não existir na placa em uso | **Crítica → mitigada nesta integração** (não falha mais o boot; ver seção 5) | `main/rtc/rtc_ds1307.c`, comentários de `ntp_kincony.h` original | Validar em hardware se o DS1307 está presente; testar log de boot no primeiro flash |
| Dois clientes SNTP concorrentes (API legada + API moderna) | Daniel × Eraldo | Comportamento indefinido de sincronização de hora, possível estado inconsistente do módulo SNTP do lwIP | **Alta → resolvida nesta integração** (cliente único, seção 5) | `rtc_ds1307.c` (`esp_sntp_*`) × `ntp_kincony.c` (`esp_netif_sntp_*`) antes da integração | Validar em hardware que apenas um evento `NTP sincronizado` aparece no log por ciclo |
| Broker MQTT com hostname/cluster ID do HiveMQ Cloud hardcoded (`MQTT_KINCONY_BROKER_URI`, não removido, apenas não referenciado) | Pré-existente | Divulgação do endpoint específico do broker (não é segredo por si, mas facilita reconhecimento/scan do endpoint) | Informativa | `main/mqtt/mqtt_kincony.h` | Remover a macro morta num próximo ciclo de limpeza; broker real já vem de NVS/painel |
| `.vscode/settings.json`/`c_cpp_properties.json` com caminhos da máquina de Daniel (`C:\Users\danmo\...`, `COM3`) | Daniel | Não afeta o firmware; apenas incomoda outros desenvolvedores no VS Code | Baixa | `.vscode/settings.json` linha `idf.monitorPort`, `clangd.arguments` | Cada desenvolvedor ajustar localmente (já é prática — Eraldo tinha `COM7` não commitado); considerar mover para `.vscode/settings.json` não versionado + `.example` versionado, fora do escopo desta integração |
| Driver I2C legado (`driver/i2c.h`) marcado End-of-Life no IDF v6.0, remoção prevista no v7.0 | Pré-existente (`entradas_kincony.c`, `saidas_digitais_kincony.c`) + usado também pelo novo `rtc_ds1307.c` de Daniel | Nenhum impacto imediato; quebra ao migrar para IDF v7 no futuro | Média | 9 warnings no build (seção 10) | Planejar migração para `driver/i2c_master.h` num próximo ciclo (não é regressão desta integração) |
| Módulo `modbus_rtu_slave_esp.c` presente mas nunca chamado em `app_main()` (em ambas as branches, antes e depois) | Pré-existente | Nenhum em runtime; apenas código morto compilado | Baixa | `main.c` não chama `ModbusRTU_Slave_Init()` | Remover ou documentar como legado/desativado num próximo ciclo |
| Painel web sem UI para RTC/timers de Daniel (só existe via comando MQTT JSON) | Daniel (funcionalidade sem UI) | Operador sem painel web não consegue configurar os 4 timers de horário sem enviar JSON manualmente | Média | Ausência de rota HTTP para `timer_set` etc. em `config_server_kincony.c`/`modbus_rs485_web.c` | Pendência funcional — não bloqueia testes, mas deveria ganhar uma página web num próximo ciclo |

## 12. Plano de testes em hardware

### Inicialização
- [ ] Energização normal (boot completo, sem crash)
- [ ] Reinicialização por software (`reset_esp` via MQTT)
- [ ] Queda e retorno de energia (verificar se RTC mantém a hora, se presente; verificar `RTC_DS1307_EstaDisponivel()` no log)
- [ ] Controlador iniciando antes do roteador (WiFi deve continuar tentando com backoff, sem travar o boot)
- [ ] Roteador iniciando antes do controlador
- [ ] Ausência prolongada de WiFi (AP de emergência deve subir; painel acessível em `192.168.4.1`)

### RTC / NTP
- [ ] Confirmar no log de boot se o DS1307 responde (`"DS1307 encontrado"` ou aviso de ausência)
- [ ] Verificar que apenas **uma** sincronização NTP ocorre por reconexão (log `"NTP sincronizado"`)
- [ ] Se o DS1307 estiver presente: desligar a energia, aguardar, religar, e confirmar que a hora foi mantida (bateria)
- [ ] Comando MQTT `rtc_sync`: confirmar resposta `"started"` e posterior atualização de `rtc_get`
- [ ] `timer_set`/`timer_get`/`timer_enable`/`timer_disable`/`timer_clear`/`timer_clear_all` via MQTT
- [ ] Timer de horário disparando `ON`/`OFF` no grupo configurado no minuto exato

### WiFi
- [ ] Conexão inicial
- [ ] Reconexão após queda de sinal (backoff exponencial, sem exceder 30s)
- [ ] Senha errada salva pelo painel → reversão automática para backup
- [ ] Nenhuma rede conhecida → AP de emergência

### MQTT
- [ ] Conexão inicial ao broker
- [ ] Perda e recuperação do broker
- [ ] Perda de WiFi (deve refletir em `Logica_Controle_AtualizarAlarme`)
- [ ] Comando no tópico `delinova/i/.../cmd`
- [ ] Confirmação de comando (ack em `delinova/o/.../state`)
- [ ] Ausência de retained em comandos e acks
- [ ] LWT "offline" retido ao desconectar abruptamente
- [ ] Nova publicação "online" (sem retain) após reconexão

### Modbus RTU
- [ ] N1200 (via canal configurado com function code/registrador/escala equivalentes)
- [ ] Sonda de oxigênio dissolvido (Renkeer RS-LDO-N01-EX — registradores conforme datasheet: sat 0x0000, O₂ 0x0002, temp 0x0004, float32 ABCD)
- [ ] Timeout de resposta
- [ ] CRC inválido / Slave ID inexistente
- [ ] Desconexão física do barramento e retorno
- [ ] Leitura negativa, escala, offset, endianness (ordem de palavra) por tipo de dado
- [ ] Limites configurados (mínimo/máximo) refletindo no MQTT

### Persistência
- [ ] Salvar configuração (WiFi/MQTT/NTP/Modbus/timers) e reiniciar — validar manutenção dos valores
- [ ] Atualizar firmware via OTA e validar que a NVS de cada módulo permanece intacta

### Estabilidade
- [ ] Teste mínimo de 24 horas
- [ ] Monitoramento de heap (`esp_get_free_heap_size`) e stack high-water mark das tasks (RTC, Modbus master, OTA)
- [ ] Resets inesperados / watchdog
- [ ] Tempo de ciclo do loop principal (200 ms) sob carga (Modbus + MQTT + RTC simultâneos)

## 13. Conclusão e decisão recomendada

**Build:** sucesso, 0 erros, 0 warnings no código próprio.
**Conflitos funcionais pendentes:** nenhum sem resolução — todos documentados e resolvidos nas seções 5, 6 e 9.
**Bloqueadores remanescentes antes de produção (não impedem testes em hardware controlado):**
- Rotação da credencial MQTT exposta no histórico (seção 11, item Crítico) — recomendado antes de expor o broker publicamente, mas não impede testes de bancada.
- Confirmação física da presença do DS1307 na placa de Eraldo (seção 5/11) — a integração é segura em ambos os cenários, mas o comportamento dos timers de horário só é validável com o hardware.

**Decisão recomendada: aprovar para testes em hardware controlado (bancada)**, com a ressalva de rotacionar a credencial MQTT antes de qualquer exposição do broker em ambiente de produção, e de executar o checklist da seção 12 — em especial os itens de RTC/NTP e Modbus (N1200 + sonda de O₂) antes de liberar para campo.
