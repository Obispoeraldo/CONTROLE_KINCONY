# Relatório — Reconexão WiFi e Portal de Configuração (AquaPulse)

Diagnóstico, arquitetura e implementação da nova lógica de reconexão/configuração WiFi.
Versão anterior preservada em `docs/wifi_reconexao_v1_legado/` (cópias `.bak`, não compiladas)
e no histórico do git (commits anteriores a esta mudança).

## 1. Diagnóstico da versão anterior

### Arquivos analisados
- `main/wifi/wifi_kincony.c` / `.h`
- `main/config_server/config_server_kincony.c` / `.h` (painel web — handler de salvar WiFi)
- `main/main.c` (boot e loop principal)
- `main/entradas_digitais/entradas_kincony.c` (para confirmar quais GPIOs já estavam em uso)

### Comportamento atual (antes desta mudança)

1. **Conexão inicial**: `Wifi_Kincony_Init()` configura o STA e registra um *event handler* do
   ESP-IDF. A reconexão em si **já era não bloqueante** — usava `WIFI_EVENT_STA_DISCONNECTED` +
   `esp_timer` com backoff exponencial (1s → dobra a cada falha → teto de 30s), sem `while`,
   `delay()` longo nem espera de `WiFi.status()`. Esse ponto specific já estava correto.
2. **Boot**: `main.c` chamava `Wifi_Kincony_EsperarResultado(30000)` — **bloqueava a task
   principal por até 30s** antes de continuar o boot (linha 61 do `main.c` original).
3. Se não conectasse em 30s: tentava restaurar um backup de WiFi salvo em NVS e reiniciar; se
   não houvesse backup, ligava um Access Point de emergência (`Wifi_Kincony_IniciarModoEmergenciaAP`,
   `WIFI_MODE_APSTA`) — **mas só neste momento único do boot**.
4. **Nenhuma lógica trazia o AP de volta se a queda de conexão acontecesse depois do boot**
   (ex.: o roteador reinicia horas depois, ou o controlador é levado para outro local com o
   equipamento já ligado há dias). Nesse cenário, o STA ficava tentando reconectar para sempre
   em segundo plano (o que é correto), mas **não havia nenhuma forma de abrir o portal** — nem
   automática, nem por botão físico (não existia nenhum botão físico mapeado no firmware).
5. **Salvar WiFi pelo painel**: `handler_post_config` gravava o novo SSID/senha em NVS
   **imediatamente**, guardava um backup do valor antigo, e **reiniciava o ESP32 em 2 segundos**
   — validação "por tentativa e erro via reboot": só depois de reiniciar é que
   `Wifi_Kincony_EsperarResultado(30000)` descobria se a rede nova funcionava; se não funcionasse,
   revertia para o backup **e reiniciava de novo**. Ou seja, a rede nunca era testada antes de
   ser gravada — só depois, via ciclo de reinicializações.

### Riscos encontrados

| # | Risco | Causa raiz |
|---|---|---|
| 1 | Portal inacessível durante reconexão prolongada mid-operação | AP só era ligado no boot, nunca depois |
| 2 | Nenhum botão físico para abrir o portal a qualquer momento | Não existia nenhuma leitura de botão de configuração no firmware |
| 3 | Troca de rede pelo painel exigia reboot(s) e podia derrubar o acesso temporariamente | Grava em NVS antes de validar; validação só via reboot |
| 4 | Teto de backoff baixo (30s) para sempre, mesmo após horas offline | Backoff exponencial sem 3º patamar mais longo |
| 5 | Sem estados nomeados/observáveis (`SEM_CREDENCIAIS`, `CONECTADO` etc.) | Só havia `bool conectado/falha` + contador de tentativas |

### O que já estava correto (mantido)
- Reconexão via `WIFI_EVENT_STA_DISCONNECTED` + `esp_timer` (não bloqueante, nunca desiste).
- `WIFI_MODE_APSTA` (e não só `WIFI_MODE_AP`) no modo de emergência — já preservava a tentativa
  de STA em paralelo, ideia mantida e generalizada nesta versão.

## 2. Arquitetura proposta

### Estados (`wifi_kincony_estado_t`, em `wifi_kincony.h`)

```
SEM_CREDENCIAIS            -- nenhum SSID configurado ainda
INICIANDO_CONEXAO          -- Init() chamado, aguardando WIFI_EVENT_STA_START
AGUARDANDO_CONEXAO         -- esp_wifi_connect() disparado, aguardando resultado
CONECTADO                  -- IP obtido (IP_EVENT_STA_GOT_IP)
DESCONECTADO               -- (reservado para uso futuro/observabilidade externa)
AGUARDANDO_NOVA_TENTATIVA  -- desconectado, esp_timer agendado para o proximo esp_wifi_connect()
PORTAL_CONFIGURACAO_ATIVO  -- AP ligado (WIFI_MODE_APSTA) - Wifi_Kincony_GetEstado() retorna
                               este estado sempre que o portal estiver aberto, independente
                               do estado STA por baixo
VALIDANDO_NOVA_REDE        -- Wifi_Kincony_TestarNovaRede() rodando
ERRO_NOVA_REDE             -- teste de rede nova falhou, credenciais antigas restauradas
```

### Reconexão permanente com intervalo progressivo (sem timeout global)

Mecanismo: `WIFI_EVENT_STA_DISCONNECTED` → incrementa contador de tentativas → calcula o
próximo intervalo → agenda `esp_timer_start_once()` → o timer chama `esp_wifi_connect()`.
**Não existe nenhum contador de tempo total nem número máximo de tentativas que faça o
firmware desistir.** Só existe o intervalo *entre* tentativas:

| Tentativas | Intervalo |
|---|---|
| 1–5 | 30 segundos |
| 6–10 | 60 segundos |
| 11+ | 5 minutos (teto, `WIFI_RETRY_INTERVALO_MAXIMO_MS`) |

O contador zera a cada reconexão bem-sucedida — cada nova queda recomeça a escada do zero.

### Portal de configuração independente da reconexão

- A interface AP é **criada e configurada uma única vez**, dentro de `Wifi_Kincony_Init()`
  (`esp_netif_create_default_wifi_ap()` + `esp_wifi_set_config(WIFI_IF_AP, ...)`), mas mantida
  **inativa** (`WIFI_MODE_STA`) até ser solicitada.
- `Wifi_Kincony_AbrirPortalConfiguracao()` só alterna `esp_wifi_set_mode(WIFI_MODE_APSTA)` —
  idempotente, não recria nada, não reinicia o WiFi, não derruba a tentativa de reconexão STA
  em paralelo. Isso evita exatamente o risco #10 do pedido ("criação repetida de Access Point").
- Duas formas de abrir, combinadas (analisadas abaixo):
  1. **Botão físico (GPIO0/BOOT), a qualquer momento** — resposta imediata, não depende de
     nenhum temporizador.
  2. **Automática após 2 minutos de desconexão contínua** (`WIFI_KINCONY_AUTO_AP_LIMIAR_MS`) —
     rede de segurança para quando o operador não tem acesso físico imediato ao botão.
- Portal aberto **automaticamente** fecha sozinho assim que a rede salva reconectar (menos
  exposição/consumo). Portal aberto **manualmente** (botão ou, no futuro, pelo próprio painel)
  só fecha por ação explícita — nunca fecha sozinho enquanto o operador está mexendo nele.

**Análise de segurança da estratégia escolhida**: manter o AP ligado o tempo todo (24/7) foi
descartado — aumentaria a superfície de exposição (rede aberta/senha fixa transmitindo
sempre) e o consumo de energia sem necessidade na grande maioria do tempo (rede íntegra). A
combinação escolhida (botão físico + auto-abertura por tempo, ambas com fechamento automático
quando aplicável) dá acesso imediato a quem tem acesso físico ao equipamento, cobre o caso de
falha prolongada sem intervenção humana, e mantém o AP desligado por padrão no dia a dia.

### Validação antes de salvar (WiFi)

`Wifi_Kincony_TestarNovaRede(ssid, senha, timeout_ms)`:
1. Guarda o SSID/senha atual (que está funcionando) em variáveis locais (RAM, nunca em NVS).
2. Troca a configuração STA em RAM para a rede candidata e chama `esp_wifi_connect()`.
3. Espera (via `xEventGroupWaitBits`, não bloqueante para o resto do firmware — ver seção 3)
   até `timeout_ms` (usado com 15s no painel web) por `WIFI_BIT_CONECTADO` ou `WIFI_BIT_FALHA`.
4. **Sucesso**: retorna `true`, a rede nova já está ativa e conectada — quem chamou decide
   gravar em NVS (só agora, pois já foi comprovada).
5. **Falha**: restaura a configuração anterior em RAM, reconecta a ela, retorna `false` — a NVS
   nunca chega a ser tocada, e a rede que já funcionava continua funcionando.

`config_server_kincony.c` (`handler_post_config`) usa isso: só grava `NVS_KEY_WIFI_SSID`/
`SENHA` se `Wifi_Kincony_TestarNovaRede()` retornar `true`. Se falhar, mostra a mensagem de
erro na própria página do painel, sem reiniciar o ESP e sem perder a rede antiga.

### Botão físico (GPIO0 / BOOT)

- GPIO0 é o botão "BOOT" nativo do ESP32, **livre neste projeto** (I2C das entradas/saídas/RTC
  usa GPIO4/GPIO15, confirmado em `entradas_kincony.c`). Não usa o botão RESET/EN (esse só
  reinicia fisicamente, não é lido por software).
- Leitura por *polling* (chamada a cada volta do loop principal, ~200ms) — sem interrupção,
  sem bloquear. Cada patamar dispara sua ação **uma única vez por pressão contínua**, assim
  que o tempo é atingido (não espera soltar o botão):

| Tempo pressionado | Ação |
|---|---|
| ≥ 5s | Abre o portal — rede atual preservada, STA continua tentando em paralelo |
| ≥ 10s | Apaga só as credenciais WiFi salvas (NVS) e abre o portal |
| ≥ 30s | Restaura toda a configuração de fábrica (WiFi, broker MQTT, NTP) e reinicia |

## 3. Diagrama de fluxo

```
                              Boot
                               |
                Config_Server_Kincony_Iniciar() (le NVS)
                               |
                    Wifi_Kincony_Init(ssid, senha)
                    (AP configurado e INATIVO; STA tenta conectar)
                               |
              Wifi_Kincony_EsperarResultado(8s) -- so p/ log do boot
                               |
                +--------------+---------------+
                |                              |
           conectou (log OK)          nao conectou em 8s
                |                              |
                |                    ha backup de WiFi bom?
                |                     /                  \
                |                  sim                    nao
                |                   |                      |
                |             restaura+reinicia      segue o boot normalmente
                |                                          |
                +--------------------+---------------------+
                                     |
                          loop principal (200ms)
        +----------------------------+-----------------------------+
        |                            |                              |
  Entradas/Saidas/Logica     Wifi_Kincony_Processar()      Wifi_Config_Button_Processar()
  Modbus/MQTT/RTC/etc.       (so verifica temporizador       (so le nivel do GPIO0)
  (nunca bloqueados)          de auto-abertura do portal)             |
                                     |                        5s/10s/30s?
                              desconectado ha                         |
                              >= 2 min?                    abre portal / apaga wifi+
                                     |                       abre portal / fabrica+reinicia
                              abre portal (auto)
                                     |
                        rede volta (IP_EVENT_STA_GOT_IP)
                                     |
                        portal era automatico? -> fecha sozinho
                        portal era manual?      -> continua aberto

  ---- em paralelo, dentro do portal (painel web) ----
  handler_post_config recebe novo SSID/senha
              |
   Wifi_Kincony_TestarNovaRede(15s)   <- so bloqueia a task do
              |                          servidor HTTP, nao a
      +-------+--------+                 task principal
      |                |
   sucesso           falha
      |                |
  grava em NVS    NAO grava nada,
  reinicia         mostra erro na
  (rede ja         pagina, rede
  comprovada)       antiga continua
                     ativa
```

## 4. Arquivos criados e alterados

| Arquivo | Tipo | O que mudou |
|---|---|---|
| `main/wifi/wifi_kincony.h` | Alterado | + `wifi_kincony_estado_t`, + funções de portal (`Abrir`/`Fechar`/`PortalAtivo`), + `Wifi_Kincony_TestarNovaRede`, + `Wifi_Kincony_Processar`, + `Wifi_Kincony_GetEstado`/`EstadoToString` |
| `main/wifi/wifi_kincony.c` | Alterado | Intervalo progressivo 30s/60s/5min (era exponencial 1s–30s); AP criado 1x e alternado por modo (não recriado); auto-abertura/fechamento do portal por desconexão prolongada; `Wifi_Kincony_TestarNovaRede` (teste em RAM sem gravar NVS) |
| `main/wifi/wifi_config_button.h` | **Novo** | Interface do botão físico de configuração |
| `main/wifi/wifi_config_button.c` | **Novo** | Leitura não bloqueante do GPIO0 (BOOT), 3 níveis de pressão (5s/10s/30s) |
| `main/config_server/config_server_kincony.h` | Alterado | + `Config_Server_Kincony_ApagarCredenciaisWifi`, + `Config_Server_Kincony_RestaurarConfiguracaoFabrica` |
| `main/config_server/config_server_kincony.c` | Alterado | Implementação das 2 funções acima; `handler_post_config` agora valida a rede WiFi (via `Wifi_Kincony_TestarNovaRede`) antes de gravar em NVS, sem reiniciar em caso de falha |
| `main/main.c` | Alterado | Espera de boot reduzida de 30s para 8s (não decide mais nada crítico); + `Wifi_Config_Button_Iniciar()`; + `Wifi_Kincony_Processar()`/`Wifi_Config_Button_Processar()` no loop principal; removida a chamada direta a `Wifi_Kincony_IniciarModoEmergenciaAP()` no boot (generalizada pelo mecanismo automático) |
| `main/CMakeLists.txt` | Alterado | + `wifi/wifi_config_button.c`, + componente `esp_driver_gpio` |
| `docs/wifi_reconexao_v1_legado/*.bak` | **Novo** | Cópias de referência dos arquivos originais (não compiladas) |
| `docs/RELATORIO_WIFI_RECONEXAO.md` | **Novo** | Este relatório |

A versão anterior completa também permanece recuperável via `git log`/`git diff` (nada foi
commitado ainda — ver seção "Controle de versão").

## 5. Build

```
idf.py build (ESP-IDF v6.0.1, target esp32): SUCESSO
CONTROLE_KINCONY.bin: 0x12e880 bytes | app partition 0x1c2000 | 33% livre
Único warning: driver/i2c.h legado marcado EOL (pré-existente, sem relação com esta mudança)
```

## 6. Forma de acionamento do portal (resumo para o operador)

1. **Botão BOOT do ESP32**, segurar:
   - 5s → abre o portal (rede atual continua tentando reconectar).
   - 10s → apaga a rede WiFi salva e abre o portal.
   - 30s → restaura toda a configuração de fábrica e reinicia.
2. **Automático**: se ficar mais de 2 minutos sem conseguir conectar, o portal abre sozinho.
3. Em qualquer um dos casos: conectar no WiFi **"Kincony-Config"** (senha `kincony2026`) e
   acessar **http://192.168.4.1**.

## 7. Procedimento de teste (hardware)

Não executado por mim — sem acesso a hardware físico, broker MQTT ou roteador de teste a
partir deste ambiente. Só a compilação foi validada. Roteiro para validação em bancada:

1. **Liga com rede disponível**: confirmar log `WiFi conectado. IP: ...` e painel acessível
   pelo IP da rede.
2. **Liga sem rede disponível**: confirmar que o boot não trava (LEDs/lógica de grupos
   continuam operando), log mostra tentativas a cada 30s, e que em até 2 min o AP
   "Kincony-Config" aparece sozinho.
3. **Rede cai durante operação**: desligar o roteador com o controlador já conectado; confirmar
   reconexão automática quando o roteador voltar (dentro de no máximo 5 min, geralmente
   bem antes se dentro dos primeiros 10 min de queda).
4. **Rede volta após minutos**: religar o roteador em ~1 min; confirmar reconexão na próxima
   tentativa agendada (≤ 30s de atraso adicional).
5. **Rede volta após várias horas**: confirmar que o firmware não reiniciou sozinho nesse
   meio tempo e que a reconexão acontece assim que o roteador volta (intervalo de até 5 min).
6. **SSID salvo deixa de existir**: confirmar que o portal abre sozinho em até 2 min, e que o
   botão físico abre imediatamente a qualquer momento antes disso.
7. **Senha da rede é alterada** (rede existe, senha errada): mesmo comportamento do item 6.
8. **Abrir o portal sem apagar a config antiga**: segurar o botão 5s; confirmar que o SSID/
   senha salvos continuam intactos em NVS e que o STA continua tentando reconectar.
9. **Cadastrar rede nova válida**: pelo portal, informar SSID/senha corretos; confirmar que
   a página mostra sucesso, o ESP reinicia e volta já conectado na rede nova.
10. **Informar rede nova inválida**: SSID/senha errados; confirmar que a página mostra o erro,
    **sem reiniciar**, e que o ESP continua conectado/tentando na rede antiga.
11. **Reiniciar durante ausência de rede**: cortar a energia com o WiFi indisponível; ao
    religar, confirmar que o boot completa normalmente (sem travar nos 8s de espera) e que a
    lógica de grupos/Modbus funciona mesmo sem WiFi.
12. **Sistema principal sem WiFi**: com o WiFi propositalmente indisponível, confirmar que
    entradas/saídas, Modbus RTU, RTC/timers e o alarme geral continuam funcionando
    normalmente (só o MQTT fica pendente).

## 8. Impactos e observações

- `Config_Server_Kincony_RestaurarBackupWifi()` (mecanismo de backup pré-existente) foi mantido
  como rede de segurança adicional no boot, mas deixou de ser o mecanismo primário de validação
  — com `Wifi_Kincony_TestarNovaRede`, credenciais ruins não chegam mais a ser gravadas pelo
  painel, então esse backup deve raramente (ou nunca) ser necessário na prática.
- O portal não implementa um servidor DNS de "captive portal" (popup automático "Fazer login
  na rede" do celular/notebook) — o termo "Portal Captive" no pedido foi tratado como "página
  de configuração acessível" (mesmo escopo que já existia no projeto: acessar
  `http://192.168.4.1` manualmente após conectar no AP). Captive DNS de verdade é um recurso
  adicional, fora do escopo desta correção; posso avaliar depois se for útil.
- Nenhum `git commit`/`push` foi feito — aguardando validação em hardware, conforme
  combinado nas mudanças anteriores desta sessão.
