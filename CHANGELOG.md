# Changelog — AquaPulse (CONTROLE_KINCONY)

Formato baseado em [Keep a Changelog](https://keepachangelog.com/pt-BR/1.0.0/).
Versionamento: [SemVer](https://semver.org/lang/pt-BR/), com pré-lançamentos `-integration.N` durante a fase de validação em hardware.

## [1.1.0-integration.1] — 2026-07-12

Integração técnica entre o desenvolvimento de Daniel Montanher (`origin/main`, publicado no GitHub) e o desenvolvimento paralelo de Eraldo Bispo (branch `painel-config-wifi`, incluindo alterações locais ainda não commitadas no momento da auditoria). Branch: `integration/eraldo-daniel`. Ver `docs/RELATORIO_INTEGRACAO_ERALDO_DANIEL.md` para a análise completa.

### Adicionado
- RTC físico DS1307 (com bateria) e 4 timers persistentes de horário para os aeradores, com comandos MQTT `timer_set/get/enable/disable/clear/clear_all`, `rtc_get`, `rtc_sync` (Daniel Montanher).
- Motor Modbus RTU **master** genérico e configurável via RS485 (N dispositivos × canais), com página web "RS485 / Modbus RTU" e ponte MQTT — substitui o teste fixo do Novus N1200 por um cadastro configurável, compatível com o N1200 e com a sonda de oxigênio dissolvido Renkeer RS-LDO-N01-EX (Eraldo Bispo).
- Sincronização de data/hora via NTP (`esp_netif_sntp`), fuso horário configurável pelo painel web (Eraldo Bispo).
- **Serviço de tempo unificado nesta integração**: o NTP corrige o RTC físico a cada sincronização bem-sucedida (callback único); o RTC mantém a hora sem internet/energia; nenhum dos dois falha o boot na ausência do outro.
- Access Point de emergência (`Kincony-Config` / `192.168.4.1`) quando nenhuma rede WiFi conhecida é encontrada (Eraldo Bispo).
- Reconexão WiFi com backoff exponencial não bloqueante (1s–30s) (Eraldo Bispo).
- Painel web de configuração (WiFi, broker MQTT, NTP, intervalo de publicação), protegido por login (Eraldo Bispo).
- Credenciais do broker MQTT e intervalo de publicação configuráveis via NVS/painel web, sem precisar regravar o firmware (Eraldo Bispo).
- Alarme geral (saída 06) por falha de grupo, perda de MQTT ou de WiFi (Eraldo Bispo).
- OTA em task própria com pilha dedicada (Eraldo Bispo).
- `README.md` com documentação de hardware, arquitetura, painel web, tópicos MQTT e tutorial de configuração Modbus RTU RS485.

### Alterado
- Tópicos MQTT separados por direção: `delinova/i/...` para comandos (entrada), `delinova/o/...` para estado/telemetria (saída) — unifica os dois esquemas usados pelas branches de origem.
- Publicação de status "online" deixou de usar retain (`MQTT_RETAIN_MONITOR`); novo `Mqtt_Kincony_PublicarRetido()` para os tópicos que precisam reter o último valor (ex.: configuração RS485).
- `RTC_DS1307_Iniciar()` não falha mais o boot quando o chip DS1307 não responde no I2C — loga aviso e desativa graciosamente os recursos de RTC/timers (mitiga risco de boot-loop em placas sem o chip físico).

### Removido
- Cliente SNTP próprio do módulo RTC (API legada `esp_sntp.h`, `RTC_DS1307_AtualizarHorarioInternet()`/`RTC_DS1307_SolicitarSincronizacaoInternet()`/task `rtc_ntp_sync`) — substituído pelo cliente SNTP único (`ntp_kincony.c`, API `esp_netif_sntp`) com callback de sincronização.
- Chamada de sincronização de hora dentro de `Wifi_Kincony_Init()` (bloqueante, antes da conexão WiFi ser confirmada).
- Parser de comando de texto legado do MQTT (`tratar_comando_grupo_texto`, `tratar_comando_geral` e auxiliares) — código morto, nunca chamado.

### Segurança
- Removidas credenciais MQTT fixas no código-fonte (`"administrador"`/`"Administrador2026"`), agora configuráveis via NVS/painel web. **A credencial antiga permanece exposta no histórico do Git (commits anteriores a `afa93fb`, já publicados) — recomenda-se rotacionar a senha no broker.**

### Riscos conhecidos
- Não foi possível confirmar fisicamente se o chip RTC DS1307 está presente na variante de placa usada por Eraldo (os comentários de código dos dois desenvolvedores se contradizem sobre isso). A integração foi projetada para funcionar corretamente em ambos os cenários, mas requer validação em hardware.
- Driver I2C legado (`driver/i2c.h`, usado por entradas/saídas digitais e pelo RTC) está marcado End-of-Life no ESP-IDF v6.0, com remoção prevista no v7.0 (pré-existente, fora do escopo desta integração).
- Painel web ainda não tem página dedicada para os timers de RTC (só acessíveis via comando MQTT JSON).

### Testes executados
- `idf.py build` (ESP-IDF v6.0.1, target `esp32`): sucesso, 0 erros, 0 warnings no código próprio. Ver seção 10 do relatório de integração para detalhamento de RAM/Flash.

### Testes ainda necessários
- Validação completa em hardware conforme checklist da seção 12 de `docs/RELATORIO_INTEGRACAO_ERALDO_DANIEL.md` (RTC/NTP, WiFi, MQTT, Modbus RTU, persistência, estabilidade 24h).

## [1.0.11] — pré-integração (origin/main, Daniel Montanher)
Última versão publicada em `origin/main` antes desta integração. Ver histórico completo em `git log origin/main`.
