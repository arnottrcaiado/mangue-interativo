# Prompt de geração — Mangue Interativo (firmware v7.11)

> **Como usar:** copie tudo a partir de "PROMPT" para uma ferramenta de IA generativa com capacidade de programação.
> Este prompt descreve o resultado **final** de um processo iterativo (V6.0 → V7.11) e incorpora as lições aprendidas
> (seção "Armadilhas conhecidas"). Preencha os campos `[...]` se quiser personalizar.

---

## PROMPT

### Papel
Atue como **Desenvolvedor Sênior especialista em IoT, ESP32, Clean Code e UX/UI para matrizes de LED**. Trabalhe de forma **analítica e baseada em evidências**: justifique decisões com documentação técnica ou medições; nunca omita aspectos técnicos para agradar. Código e comentários em **português**, com documentação educativa (o "porquê", não só o "o quê").

### Objetivo
Gerar o firmware completo (arquivo único `.ino`, Arduino/arduino-esp32 2.x e 3.x) do **"Mangue Interativo"**: uma planta física inteligente com rosto expressivo numa matriz RGB 8×8, que reage ao ambiente, expressa emoções, tem uma página web de controle e sinaliza o estado do sistema seguindo princípios de UX.

### Hardware
- Placa **Waveshare ESP32-S3-Matrix**: matriz WS2812B 8×8 no **GPIO14** (ordem de bytes **RGB**, não GRB); IMU **QMI8658** em I2C **SDA 11 / SCL 12**, endereço **0x6B** (alternativo 0x6A); botão **BOOT = GPIO0** (LOW = pressionado, usável após o boot).
- Opcionais, **detectados automaticamente** (ausência ≠ erro):
  - Sensor de umidade do solo capacitivo no **GPIO4** (ADC1; ADC2 conflita com o Wi-Fi). Leitura com `analogReadMilliVolts`, 8 amostras, calibração `SOLO_MV_SECO=2800`, `SOLO_MV_MOLHADO=1200`; sede com histerese (entra < 30%, sai > 40%). Leituras < 50 mV ou > 3150 mV por 3 vezes → "ausente".
  - **HC-SR04**: TRIG GPIO5, ECHO GPIO6 com **divisor 1 kΩ/2 kΩ** (ECHO é 5 V). Medição por **interrupção CHANGE** no ECHO (proibido `pulseIn`), ciclo 70 ms, mediana de 3, 58 µs/cm, > 25 ms = "nada". Sem resposta em 10 disparos → ausente; enquanto não decidir, estado "a verificar" com sondagem rápida; depois de ausente, sondar a cada 2 s. Pino ECHO com `INPUT_PULLDOWN`.
- Futuro (não implementar, só deixar ganchos documentados): APDS9960, DHT.

### Restrições de arquitetura
1. **Proibido `delay()`.** Tudo com `millis()`; única espera admitida: pulso de 10 µs do TRIG. Comparações de tempo seguras contra overflow.
2. `WebServer` + **WiFiManager não bloqueante** (`setConfigPortalBlocking(false)`, `wm.process()` no loop). O servidor próprio só faz `begin()` quando há conexão e o portal **não** está ativo (o portal usa a porta 80).
3. **Máquinas de estado** com `enum class`: `Emocao`, `StatusHardware`, `ModoEcra {ACORDAR, ROSTO, ADORMECER, PROTETOR}`, `EstadoRede {A_LIGAR, PORTAL, LIGADO, PERDIDO}`, `Saude {OK, A_LIGAR, AUSENTE, DESATIVADO, AVISO, ERRO}`, `Nivel {OK, INFO, AVISO, ERRO}`.
4. **Modularidade**: `#define` e estado no topo; seções numeradas (Configuração, Tipos/Estado, Utilitários, Emoções & Energia, Saúde do sistema, QMI8658, HC-SR04, Solo, Visual-respiração/cor, Visual-rosto, Protetores, Modos/Avisos, Compositor, Rede/Web, Diagnóstico, setup/loop). Dados em **tabelas** com `static_assert` de completude.
5. Sem alocação dinâmica no caminho quente; JSON com `snprintf`; HTML em `PROGMEM` (raw string).
6. Configurações persistentes em `Preferences` (NVS): brilho, paleta, cor única, ordem de cor.

### QMI8658 (I2C não bloqueante)
Máquina de estados: PROCURAR (WHO_AM_I `0x00` = `0x05` em 0x6B/0x6A) → reset (`0x60` ← `0xB0`) → aguardar 20 ms sem bloquear → configurar `CTRL1 0x02 = 0x40` (auto-incremento), `CTRL2 0x03 = 0x16` (±4 g, 125 Hz), `CTRL7 0x08 = 0x01` (só acelerômetro), **ler de volta** CTRL2 → ATIVO (ler 6 bytes a partir de `0x35` a cada 10 ms; 8192 LSB/g; ignorar amostra toda zero). `Wire.setTimeOut(10)`. Erro após 3 falhas consecutivas; nova tentativa com backoff exponencial 0,5 → 8 s.
Processamento: gravidade por passa-baixa (τ 80 ms); dinâmica = |a − gravidade|; agitação com decaimento (τ 300 ms). **Calibração automática do repouso** (50 amostras estáveis) — a inclinação é o desvio em relação ao repouso (funciona com a placa deitada ou de pé). Movimento com histerese (entra 0,40 g, sai 0,28 g). Interação se dinâmica > 0,08 g; susto se > 0,8 g (refratário 800 ms). Constantes para trocar/inverter eixos.

### Rotação automática do rosto (orientação pela gravidade)
- Com a matriz de pé, o "baixo" do rosto aponta para o chão: φ = atan2(−g_x, g_y) sobre a gravidade filtrada no plano da matriz (0° normal, 90° chão à esquerda, 180° de cabeça para baixo, 270° chão à direita).
- Só considerar se a matriz estiver a ≤ 40° da vertical (|g_plano|/|g| ≥ cos 40°); deitada → manter.
- Rodar para o quarto q = round(φ/90) **apenas** se |φ − 90q| ≤ **15°**; entre janelas manter a última orientação (histerese). Exigir **0,4 s** de estabilidade. Giro **animado** (450 ms, smoothstep, caminho mais curto).
- Compor a imagem num buffer **"vista"** (como a pessoa vê; é o que o espelho web mostra) e só no fim rodar para os LEDs: permutação exata em repouso; amostragem bilinear durante a animação. Tudo roda junto (rosto, painel, indicador, avisos, anel).
- Converter a inclinação para o referencial do rosto (rotação de −φ) antes da física das pupilas; ao mudar de orientação, **recalibrar o repouso** e zerar movimento.
- **Montagem do sensor**: nesta placa o QMI8658 está rodado face à matriz. Aplicar `v_matriz = R(q·90°)·M·v_sensor` (M = inverter X se espelho) com padrão **q = 1 (+90°)**, sem espelho (validado numa placa real: sem correção o rosto ficava sempre 90° anti-horário). Guardar `q`/espelho em NVS. Manter também um vetor "cru" filtrado (sem correção) para calibrar.
- **Calibração completa (2 passos)** `/api/orientacao?passo=1|2|repor`: passo 1 placa de pé e rosto direito (φ1); passo 2 rodada 90° para a esquerda (φ2). Hipótese rotação θ = −φ1 (confere φ2+θ ≈ 90°); hipótese espelho θ' = φ1 (confere −φ2+θ' ≈ 90°); escolher a de menor erro (≤ 30°) e arredondar a 90°. Recusar (409, com mensagem) se a placa não estiver de pé/parada ou se as posições não distarem 90°.
- **Girar ↺↻ e fixar 🔒** (modelo do bloqueio de rotação do celular): `/api/girar?sentido=±1` gira o rosto 90° **já**; em modo automático soma ±1 à correção da montagem (a planta aprende), em modo fixo muda a posição fixa. `/api/rotacao?auto=0` fixa **na posição atual** (não volta a 0°); `auto=1` volta à automática. Persistir `autorot`, `rotfix`, `eixq`, `eixm`.
- **BOOT**: toque = painel; **duplo toque (≤ 350 ms)** = modo girar (ícone ↻ e 4 cantos a pulsar); nele, toque = girar ↻, **segurar 1,5 s** = fixar/soltar (anel rápido + ícone de cadeado laranja/aberto verde) e sair; 10 s sem tocar = sai. Segurar 5 s fora do modo = portal Wi-Fi.
- Página: segmentado 🔄 Automática / 🔒 Fixa, botões ↺ ↻, dica contextual, posição, ângulo medido, montagem do sensor, botão "Calibração completa" (assistente com ícones e mensagens), "Repor montagem de fábrica". JSON: `rot {auto, graus, medido, vertical, eixos, espelho, modo_girar}`.

### Emoções, energia e comportamento
9 emoções: `ESPERA, ALEGRIA, SEDE, SUSTO, AMOR, RAIVA, SONO, CURIOSO, CONFUSO`.
Prioridade: **transitória** (SUSTO 3 s; RAIVA 6 s quando há 3 sustos em 10 s; ALEGRIA 2,5 s de "alívio"; CONFUSO 4 s ao perder Wi-Fi) > **manual** (web, padrão 10 s) > **base**: SEDE (solo seco) > SONO (energia < 20) > AMOR (pessoa presente > 8 s e energia > 50) > CURIOSO (pessoa < 60 cm, sai > 75 cm) > ALEGRIA (interação < 20 s) > ESPERA.
"Pessoa" = algo perto que **varia** (> 1 cm); objeto imóvel deixa de contar após 45 s. Aproximação brusca (> 25 cm em 300 ms e < 30 cm) = susto.
**Energia** 0–100 (início 70), atualizada 1×/s: −1/min; +0,6/s com interação recente; −2/min extra com sede; +3/min no protetor; −4 por susto. Modula ritmo respiratório (×0,8–1,2), amplitude e intensidade.

### Motor visual (100 FPS, `INTERVALO_FRAME_MS 10`)
**Rosto procedural** (não bitmaps por emoção):
- Olhos 3×3 (olho esquerdo definido, direito espelhado; linhas 1–3, colunas 0–2 e 5–7): NORMAL `111/111/111` (pupila), FELIZ `010/101/000`, SEMICERRADO `000/111/111` (pupila), FECHADO `000/000/111`, CORACAO `101/111/010`, X `101/010/101`, ZANGADO `100/110/111` (pupila).
- Bocas (linhas 5–7): NEUTRA, SORRISO, TRISTE, O, ONDULADA, DENTES, PEQUENA, TORTA, BEIJO. Sobrancelhas na linha 0.
- Tabela de expressões (olhoE, olhoD, boca, sobrancelhas, cor RGB, respirações/min, amplitude, intensidade):
  espera NORMAL/NORMAL/NEUTRA (90,90,110) 10 0,08 0,80 · alegria FELIZ/FELIZ/SORRISO (0,170,30) 14 0,10 1,0 · sede SEMICERRADO/SEMICERRADO/ONDULADA (170,80,0) 8 0,06 0,80 · susto NORMAL/NORMAL/O, sobr. `11100111` (120,0,200) 24 0,10 1,0 · amor CORACAO/CORACAO/BEIJO (220,20,100) 12 0,12 1,0 · raiva ZANGADO/ZANGADO/DENTES (220,40,0) 22 0,10 1,0 · sono FECHADO/FECHADO/PEQUENA (40,50,140) 6 0,06 0,60 · curioso NORMAL/NORMAL/PEQUENA (5,173,96) 16 0,08 0,95 · confuso NORMAL/SEMICERRADO/TORTA, sobr. `11100000` (160,140,0) 12 0,08 0,90.
- **Pupila escura** anti-aliased (bilinear). **Física mola-amortecedor**: k = 14, c = 6,5 (ζ ≈ 0,87), ganho de inclinação 30, ricochete −0,4 nas bordas. Alvo: pessoa presente → centro (e **vesgo** a < 12 cm, convergência 0,7); sem estímulo → sacadas aleatórias (±0,6, ±0,4) a cada 3–7 s. Se houver inclinação ou pessoa, olhos FELIZ passam a NORMAL (senão o olhar fica invisível).
- Pálpebras: despertar (1,4 s) e adormecer (2,2 s) suaves; **pestanejar desligado** por padrão (`PISCAR_OLHOS 0`; se ligado, 4–9 s, sem piscada dupla).
- Crossfade de rosto 900 ms (smoothstep).

**Camada base — respiração por TAMANHO (não por brilho):** fase acumulada (`fase += dt·ritmo/60000`, sem saltos ao mudar o ritmo); onda cosseno; escala = 1 − amplitude·(1 − onda); **reamostragem por área (box filter)** em torno do centro (4,4) com **compensação de área ∝ 1/s²**; modulação de brilho de apenas ±3%. Ritmo/amplitude filtrados (τ 3 s / 2 s). Erro do IMU → respiração "ofegante" (26/min).

**Cor:** apenas a emoção (paleta) define a cor do rosto. Interpolação em **OKLCh** (conversões OKLab de Ottosson; LEDs tratados como RGB linear), τ 1,2 s, matiz pelo caminho mais curto. Iniciar já na cor da paleta.

**Brilho:** `BRILHO_BASE 0,22`; **brilho geral** 5–100% (padrão 40%) multiplica tudo; piso de 1 nível nos canais ativos (o rosto nunca apaga). **Dithering temporal** sigma-delta por canal (zera em pixels apagados).

**Paletas** (web, persistidas): mangue (tabela acima), oceano, pôr do sol, pastel, floresta, **cor única** (seletor; rejeitar cores com canal máximo < 40). Requisitos medidos em OKLab: quaisquer duas emoções ΔE ≥ 0,085; todas ΔE ≥ 0,11 do vermelho (200,0,0) e do ciano (0,150,200); ESPERA e ALEGRIA de cada paleta ΔE ≥ 0,15 da paleta original (senão a troca "não se nota"). Valores usados no firmware v7.7, extraídos do código (ordem: espera…confuso):
- oceano: (4,99,254) (0,218,175) (200,170,90) (140,80,255) (230,90,200) (255,90,70) (30,40,140) (137,233,255) (149,167,186)
- pôr do sol: (255,143,81) (255,190,30) (186,92,22) (200,40,160) (255,60,120) (255,55,0) (110,40,140) (255,230,120) (192,128,146)
- pastel: (155,189,232) (150,220,106) (234,179,129) (188,112,254) (255,151,241) (255,122,123) (110,140,220) (130,255,227) (255,248,156)
- floresta: (107,166,93) (93,242,63) (196,140,37) (150,90,200) (230,90,130) (210,89,20) (40,80,70) (178,228,106) (160,119,87)
  *(se recalcular, valide os limiares acima e reporte os valores obtidos).*

**Protetor de tela** (após 90 s sem interação, sem emoção manual/transitória/portal/painel/pessoa): pálpebras fecham → fade 1,5 s → rotação a cada 40 s entre **Maré** (plasma de senos, paleta água/mangue, lento), **Jogo da Vida** (toro 8×8, 1 geração/700 ms, fade, reinício se estagnar/período 2/150 gerações, cor por idade) e **Vaga-lumes** (5, Lissajous, brilho cosseno², rastro). Qualquer interação acorda.

**Teste de cores** (web): 6 s com a matriz inteira em vermelho → verde → azul puros.

### Sinalização de sistema (UX) — canal próprio, 3 camadas
Princípios a cumprir e documentar no código: um canal = um significado (o rosto **nunca** muda de cor por falha); **não usar só cor** (WCAG 1.4.1: cor + padrão); **ausente ≠ erro**; *debounce*; **tecnologia calma** (reagir e acalmar; repetir só quando alguém dá atenção); **reconhecer, diagnosticar, recuperar** (Nielsen #9).
- **Modelo único de saúde** por componente (IMU, Wi-Fi, solo, ultrassom), avaliado a cada 250 ms; histórico circular de 12 eventos (t, componente, de, para). Wi-Fi: LIGADO = OK; A_LIGAR; PORTAL = AVISO (precisa de ação); PERDIDO = AVISO **só após 5 s**.
- **Camada 1 — indicador** no pixel (7,7) (livre em todos os rostos — verificar): apagado = OK; azul (0,110,255) pulsando (cosseno, 4 s) = INFO; âmbar (255,130,0) pulsando = AVISO; **vermelho fixo** = ERRO; brilho 0,25 × geral. No protetor, esconder INFO.
- Reações: Wi-Fi em AVISO → CONFUSO 4 s + **ícone Wi-Fi uma vez** (3 camadas: ponto, arco médio, arco grande, desenhadas com fade; se perdido, os arcos maiores "perdem força"; 4 s; nunca piscar). Repetir o ícone **apenas** quando houver interação e > 60 s desde o último. Wi-Fi volta de AVISO → ALEGRIA (alívio). Erro do IMU → olhos em X.
- **Camada 2 — painel de estado na matriz**: 4 ícones 3×3 nos cantos — Wi-Fi ↖ barras `001/011/111`, Movimento ↗ cruz `010/111/010`, Solo ↙ gota `010/111/111`, Distância ↘ onda `001/101/001`; cores verde (0,200,70) / azul / cinza (60,60,60) / cinza escuro (22,22,22) / âmbar / vermelho; INFO e AVISO pulsando; ícones acendem um a um (250 ms). Abre: **ao ligar** (verificação inicial: mínimo 2,5 s, termina 0,8 s depois de nada estar "a ligar", máximo 6 s; só depois as pálpebras abrem), **toque curto no BOOT** (6 s; segundo toque fecha), **botão web** (`/api/painel`) e **sozinho** num problema novo de componente não-Wi-Fi (5 s, culpado em destaque, outros a 30%), relembrado na interação após 60 s se persistir. Enquanto aberto, esconder o indicador e impedir o sono.
- **Camada 3 — recuperação**: BOOT pressionado > 0,8 s mostra **anel de progresso** na moldura (28 pixels, sentido horário, cor (160,190,255)); aos **5 s** abre o portal WiFiManager por 180 s (antes: `server.stop()`, `MDNS.end()`); soltar antes cancela. Debounce 30 ms.

### Página web (uma página, sem dependências externas)
Tema por variáveis CSS derivado da paleta (fundo/cartões/botões a partir da cor de ESPERA; destaque = cor da emoção atual; transição 1,2 s). Conteúdo:
- Título com **selo** do nível geral (texto + cor: Tudo bem / Iniciando / Atenção / Erro).
- Faixa **"Sem conexão com a planta"** após 4 falhas de fetch (some sozinha).
- **Espelho ao vivo** da matriz (`/api/matriz` a cada 250 ms; brilho normalizado, gama 0,6).
- **Saúde do sistema**: mini-painel com **os mesmos 4 ícones e cores** da placa + legenda das posições + explicação do BOOT + botão "Mostrar na placa"; linha por componente com ícone, estado, "há X" e **o que fazer**; legenda do indicador; histórico em `<details>`.
- Estado (emoção, modo, sistema, Wi-Fi, solo, distância, inclinação, respiração, barra de energia).
- Emoções manuais, Automático, Acordar, Protetor, Calibrar repouso, controle **Brilho geral** (envia ao soltar).
- Paletas (select + seletor de cor + amostras com a emoção atual destacada).
- Orientação do rosto: ver secção "Rotação automática" (segmentado, ↺ ↻, calibração completa).
- Cores dos LEDs: Testar cores (mostra "A placa deve estar: VERMELHO/VERDE/AZUL") + seletor de ordem (RGB, GRB, BRG, RBG, GBR, BGR, via `updateType`) + dica de diagnóstico.
- Rodapé: uptime, RSSI, heap, loop máx (µs), FPS.

### API
GET `/`, `/api/estado`, `/api/saude`, `/api/matriz`; POST `/api/rotacao?auto=`, `/api/girar?sentido=`, `/api/orientacao?passo=`, `/api/emocao?nome=&duracao=`, `/api/brilho?valor=`, `/api/paleta?nome=&cor=`, `/api/ordem?nome=`, `/api/teste_cor`, `/api/painel`, `/api/acordar`, `/api/protetor`, `/api/calibrar`. Validar parâmetros (HTTP 400 com mensagem). mDNS `mangue.local`. AP de configuração `Mangue_Interativo_AP`.

### Diagnóstico
Reinícios: motivo (`esp_reset_reason`) e módulo onde ocorreu o crash (migalhas RTC), contagem de crashes seguidos, pilha livre mínima — no Monitor Série (`[DIAG]`) e no rodapé da página.
Serial 115200 com prefixos (`[IMU]`, `[US]`, `[SOLO]`, `[REDE]`, `[WEB]`, `[EMO]`, `[SAUDE]`, `[BOTAO]`, `[ESTADO]` a cada 5 s). Medir loop máximo (µs) e FPS por janela de 1 s.

### Armadilhas conhecidas (obrigatório evitar)
1. **Não usar `setBrightness()` no loop** (é "lossy"; com valores baixos apaga cores fracas). Calcular o brilho por pixel.
2. Matriz desta placa é **RGB**; com `NEO_GRB` vermelho e verde trocam (e o espelho web não mostra o problema).
3. Pulsar o brilho (coração) cansa a vista — use tamanho/respiração; meça a luminância.
4. Interpolar cor em RGB escurece/acinzenta a transição — use OKLCh.
5. Brilho baixo + 8 bits = degraus visíveis — use dithering temporal a ≥ 100 FPS.
6. Não sinalizar falhas mudando a cor do rosto; sensor opcional ausente não é erro.
7. Portal WiFiManager e `WebServer` disputam a porta 80.
8. ADC2 não funciona com Wi-Fi; ECHO do HC-SR04 é 5 V.
9. Olhos sem pupila escondem o olhar — trocar para olhos abertos durante estímulo.
10. Estado inicial de cor deve ser a paleta (não um ciano "de arranque").
11. Rotação sem histerese/estabilidade faz o rosto oscilar perto de 45°; sem recalibrar o repouso, a nova posição parece "inclinação permanente" (olhar preso, planta nunca dorme).
12. Não escreva valores numéricos (paletas, limiares) de memória na documentação: extraia-os do código.
14. **Pilha**: o `loop()` tem 8 KB e o `show()` da Adafruit NeoPixel (IDF 5) reserva `numBytes·8·4` B na pilha (~6 KB para 64 LEDs) → reinício em ciclo `rst:0xc`. Use `SET_LOOP_TASK_STACK_SIZE(16 * 1024)`, chame `show()` diretamente do `loop()`, torne estáticos os buffers grandes (frames, JSON) e meça `uxTaskGetStackHighWaterMark`. Teste no PC não apanha isto: meça com `-fstack-usage`.
15. Instrumente reinícios: `esp_reset_reason()` + "migalhas" em `RTC_NOINIT_ATTR` (etapa do loop antes de cada módulo, contador de crashes seguidos, validação por número mágico) e repita a mensagem `[DIAG]` no Monitor Serial durante o 1.º minuto (a USB nativa reconecta a cada reset).
16. ISR do ECHO sem acesso à flash (ler `GPIO_IN_REG`), porque escritas na NVS desligam a cache.
13. Não assuma que os eixos do acelerómetro coincidem com os da matriz (nesta placa estão a 90°); o acelerómetro mede a reação "para cima" (soma 180°). Uma rotação de montagem dá erro constante; um espelho dá erro que alterna — ofereça correção por manipulação direta e calibração em 2 posições.

### Verificação exigida (antes de entregar)
Construir um **banco de testes no PC** que compila o `.ino` real com *stubs* (Arduino, Wire emulando o QMI8658, WiFi, WiFiManager, WebServer, ESPmDNS, Preferences, e um Adafruit_NeoPixel que **reproduz a aritmética de brilho da biblioteca e a ordem dos bytes no fio** de uma matriz física RGB). Verificar, no mínimo:
- inicialização do IMU sem bloqueio; calibração; despertar após a verificação inicial (≤ 6 s);
- rosto nunca apaga (9 emoções × 150 frames); respiração contínua; luminância percebida sem saltos > 5% entre frames;
- SUSTO, RAIVA, CURIOSO, AMOR, vesgo, SEDE com histerese, SONO, protetor e despertar;
- inclinar devagar não causa susto; olhar segue a inclinação também em ALEGRIA;
- HC-SR04 ausente/presente detectado; objeto imóvel ignorado após 45 s; solo ausente ≠ erro;
- paletas: limiares de ΔE; LEDs físicos = espelho com RGB e trocados com GRB;
- sinalização: indicador apagado quando OK, vermelho **fixo** no erro, âmbar pulsando no Wi-Fi; *debounce* de 5 s; CONFUSO termina; ícone não se repete em 40 s sem ninguém; relembra na interação; rosto mantém a cor da paleta durante erro;
- painel: ícones sem sobreposição; BOOT curto abre/fecha; erro novo abre sozinho com destaque; anel ~50% aos 2,9 s; soltar cancela; 5 s abre o portal e libera a porta 80; servidor volta sozinho;
- montagem: simular o chip rodado (−90°) e espelhado; reproduzir o defeito sem correção (rosto a 270° com a placa direita); correção de fábrica acerta as 4 posições; calibração de 2 passos encontra rotação, espelho e 180°; recusa placa deitada/posições não a 90°;
- girar/fixar: um toque em ↻ endireita e aprende (4 posições certas depois); ↺ desfaz; fixo mantém a posição; duplo toque/toque/segurar 1,5 s no BOOT; saída por inatividade; toque simples continua a abrir o painel;
- rotação: 0° de pé; 90° após 0,4–0,7 s com giro ≤ 8°/frame terminando exato; LEDs físicos = vista rodada (90°: físico (x,y) = vista (y, 7−x); 180°: (7−x, 7−y)); janela de 15° e histerese a 40°; inclinação breve não roda; placa deitada não roda; olhar correto no referencial rodado; repouso recalibrado; girar à mão (90° em 0,4 s) **não** causa susto; desligar volta a 0° e fixa;
- APIs válidas/ inválidas (400); JSON válido.
Medir e reportar: modulação de luminância por emoção, pior salto entre frames e quedas bruscas. Testar a página num navegador real (servidor de imitação + puppeteer) e gerar pré-visualizações PNG/GIF **a partir do próprio motor**.

### Entregáveis
1. `Mangue_Interativo_V7/Mangue_Interativo_V7.ino` comentado (cabeçalho com resumo, checklist de calibração e uso do BOOT).
2. `banco_de_testes/` (stubs, testes, métricas, renderizador, teste da página).
3. `preview/` (rostos, paletas, olhar/avisos, painel de estado, GIFs).
4. `README.md` (autoria `[nome]`, notas de IAG, ligações, instalação, uso, API, arquitetura, evidências, limitações).

### Forma de resposta
Comece por um diagnóstico breve e quantificado; explique cada decisão com evidência; declare explicitamente o que **não** foi possível verificar (ex.: compilação no toolchain real e testes na placa). Sem afirmações otimistas sem medição.
