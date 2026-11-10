// Banco de testes no PC: compila o .ino real com stubs, verifica comportamentos
// e exporta frames (para pré-visualização). Uso: ./teste <pasta_saida>
#include "../Mangue_Interativo_V7/Mangue_Interativo_V7.ino"
#include <vector>
#include <fstream>
#include <array>
#include <utility>

unsigned long simMillis = 0;
int simSoloMv = 1800;        // ~62% de humidade
int simBotao = HIGH;         // BOOT solto
esp_reset_reason_t simMotivoReinicio = ESP_RST_POWERON;
unsigned simPilhaLivre = 9000;
SerialStub Serial;
EspStub ESP;
TwoWire Wire;
SimQMI simQmi;
WiFiStub WiFi;
MDNSStub MDNS;

static uint8_t ultimo[NUM_LEDS][3];
static int showForaDoLoop = 0, showsTotal = 0;
static void capturar(const Adafruit_NeoPixel& m) {
  memcpy(ultimo, m.px, sizeof(ultimo));
  showsTotal++;
  // show() so pode vir do loop() (ou do setup(), com a pilha vazia)
  if (migalhas.etapa != (uint8_t)Etapa::LEDS && migalhas.etapa != (uint8_t)Etapa::ARRANQUE) showForaDoLoop++;
}

static int falhas = 0;
#define VERIFICA(cond, msg) do { bool _ok = (cond); printf("  [%s] %s\n", _ok ? "OK " : "FALHA", msg); if (!_ok) falhas++; } while (0)

// Simula o HC-SR04: cada disparo devolve um eco pelo mesmo caminho da ISR
static float simDistCm = 999; static bool simUsLigado = true;
static void avancar(unsigned long ms) {
  for (unsigned long i = 0; i < ms; i++) {
    simMillis++;
    if (simUsLigado && us.aguardarEco && !ecoPronto && millis() - us.tDisparo >= 2) {
      ecoDuracaoUs = simDistCm >= 999 ? 38000UL : (unsigned long)(simDistCm * 58); ecoPronto = true;
    }
    loop();
  }
}

// ---- exportação de frames: cada linha = "etiqueta;r,g,b;r,g,b;..."
static std::ofstream saida;
static void exportar(const char* etiqueta) {
  saida << etiqueta;
  for (int i = 0; i < NUM_LEDS; i++) saida << ';' << (int)ultimo[i][0] << ',' << (int)ultimo[i][1] << ',' << (int)ultimo[i][2];
  saida << '\n';
}
static int somaFrame() { int s = 0; for (int i = 0; i < NUM_LEDS; i++) s += ultimo[i][0] + ultimo[i][1] + ultimo[i][2]; return s; }
// Avança 'ms' e exporta o frame mais brilhante (pico do coração)
static void exportarPico(const char* etiqueta, unsigned long ms) {
  uint8_t melhor[NUM_LEDS][3]; int maxS = -1;
  avancar(DURACAO_CROSSFADE_MS + (unsigned long)(5 * TAU_COR_MS));   // esperar cor e rosto assentarem
  for (unsigned long t = 0; t < ms; t += INTERVALO_FRAME_MS) {
    avancar(INTERVALO_FRAME_MS);
    int s = somaFrame(); if (s > maxS) { maxS = s; memcpy(melhor, ultimo, sizeof(melhor)); }
  }
  memcpy(ultimo, melhor, sizeof(ultimo));
  exportar(etiqueta);
}
static void pedirEmocao(const char* nome, const char* dur = "600") {
  server.argNome = String(nome); server.argDur = String(dur); rotaEmocao();
}
static void distancia(float cm) { simDistCm = cm; }

// =================== V6: reprodução exata da lógica de brilho ===================
static uint8_t v6Batimento(unsigned long agora) {
  int ciclo = agora % 1200; float onda = 0;
  if (ciclo < 150) onda = sin(ciclo * PI / 150.0);
  else if (ciclo >= 250 && ciclo < 400) onda = sin((ciclo - 250) * PI / 150.0);
  return 5 + (onda * 35);
}
static long arduinoMap(long x, long a, long b, long c, long d) { return (x - a) * (d - c) / (b - a) + c; }

static void testesV6() {
  printf("\n== Evidencias sobre a V6.0 ==\n");
  struct { const char* nome; uint8_t r, g, b; } cores[] = {
    {"ESPERA (40,40,40)", 40, 40, 40}, {"ALEGRIA (0,150,0)", 0, 150, 0}, {"TENTANDO (0,150,200)", 0, 150, 200}};
  for (auto& c : cores) {
    Adafruit_NeoPixel m(64, 0, 0); int apagado = 0, total = 0, minMax = 255;
    for (unsigned long t = 0; t < 1200; t += 30) {
      m.setBrightness(v6Batimento(t)); m.setPixelColor(0, c.r, c.g, c.b);
      int mx = max(m.px[0][0], max(m.px[0][1], m.px[0][2]));
      minMax = min(minMax, mx); if (mx == 0) apagado++; total++;
    }
    printf("  %-22s frames com rosto APAGADO: %2d/%d (%.0f%%), valor min do canal mais forte: %d\n",
           c.nome, apagado, total, 100.0 * apagado / total, minMax);
  }
  int hist[8] = {0};
  for (int i = -100; i <= 100; i++) { long p = arduinoMap(i, -100, 100, 0, 7); hist[min(7L, max(0L, p))]++; }
  printf("  map(-100..100 -> 0..7): ocorrencias por coluna: ");
  for (int i = 0; i < 8; i++) printf("%d ", hist[i]);
  printf("\n  (coluna 7 so e atingida com eixo exatamente = 1.00; eixoY nunca e atualizado -> py fixo em %ld)\n",
         arduinoMap(0, -100, 100, 0, 7));
}

// =================== V7 ===================
static void testesV7(const char* pasta) {
  printf("\n== Testes da V7.0 ==\n");
  saida.open(std::string(pasta) + "/frames.txt");
  matriz.aoMostrar = capturar;
  WiFi.estado = WL_CONNECTED;
  setup();

  // 1. Arranque: ACORDAR -> ROSTO, IMU sucesso e calibrado
  // Verificacao inicial: painel com 4 icones antes do rosto
  avancar(150);
  { int verdes = 0; for (int i = 0; i < NUM_LEDS; i++) verdes += espelho[i][1] > espelho[i][0];
    VERIFICA(painel.ativo && painel.arranque && verdes > 0, "Ao ligar: painel de verificacao (checklist) aparece antes do rosto"); }
  VERIFICA(subs[(uint8_t)Sub::ULTRASSOM].saude == Saude::A_LIGAR || subs[(uint8_t)Sub::ULTRASSOM].saude == Saude::OK,
           "Ao ligar, sensor ainda nao testado aparece 'a verificar' (nao 'nao instalado')");
  for (int i = 0; i < 30; i++) { avancar(100); char e[40]; snprintf(e, 40, "gif:ao ligar: verificacao %d", i); exportar(e);
    if (i == 12) { exportar("painel:ao ligar (tudo ok)"); } }
  for (int i = 0; i < 16; i++) { avancar(100); char e[32]; snprintf(e, 32, "gif:acordar %d", i); exportar(e); }
  VERIFICA(imu.status == StatusHardware::SUCESSO, "QMI8658 inicializado sem bloquear (WHO_AM_I, reset, CTRL lidos de volta)");
  VERIFICA(imu.calibrado, "Posicao de repouso calibrada automaticamente");
  VERIFICA(vis.modo == ModoEcra::ROSTO, "Sequencia de despertar terminou em ROSTO");
  VERIFICA(!painel.ativo, "Verificacao inicial termina sozinha (<= 6 s) e da lugar ao rosto");
  VERIFICA(rede.servidorAtivo, "Servidor web iniciado apos ligacao Wi-Fi");

  // 2. Piso de brilho: em 10 s, nenhum pixel do rosto apaga em nenhuma emocao
  bool pisoOk = true;
  for (uint8_t e = 0; e < (uint8_t)Emocao::TOTAL; e++) {
    pedirEmocao(EXPRESSOES[e].nome);
    avancar(600);
    for (int f = 0; f < 150; f++) {
      avancar(INTERVALO_FRAME_MS);
      float buf[NUM_LEDS] = {0};
      desenharRosto(buf, expressaoDe(emo.atual), false, millis());
      for (int i = 0; i < NUM_LEDS; i++)
        if (buf[i] > 0.99f && max(ultimo[i][0], max(ultimo[i][1], ultimo[i][2])) == 0) pisoOk = false;
    }
  }
  VERIFICA(pisoOk, "Rosto nunca apaga (9 emocoes x 150 frames, piso >= 1)");

  // 3. Respiracao: tamanho muda suavemente e o brilho quase nao varia
  pedirEmocao("espera"); avancar(6000);
  float maxDEsc = 0, escAnt = vis.escala, minEsc = 1, maxEsc = 0;
  float lmin = 1e9, lmax = 0, piorSaltoL = 0, lAnt = -1;
  pedirEmocao("susto");
  for (int f = 0; f < 8000 / INTERVALO_FRAME_MS; f++) {
    avancar(INTERVALO_FRAME_MS);
    maxDEsc = max(maxDEsc, fabsf(vis.escala - escAnt)); escAnt = vis.escala;
    minEsc = min(minEsc, vis.escala); maxEsc = max(maxEsc, vis.escala);
    static float Lp = -1; float L0 = 0; for (int i = 0; i < NUM_LEDS; i++) L0 += 0.2126f * ultimo[i][0] + 0.7152f * ultimo[i][1] + 0.0722f * ultimo[i][2];
    Lp = Lp < 0 ? L0 : Lp + (L0 - Lp) * (1 - expf(-INTERVALO_FRAME_MS / 40.0f)); float L = Lp;   // como o olho integra
    if (f > 3000 / INTERVALO_FRAME_MS) { lmin = min(lmin, L); lmax = max(lmax, L); if (lAnt > 0) piorSaltoL = max(piorSaltoL, fabsf(L - lAnt) / lAnt); lAnt = L; }
  }
  printf("  escala do rosto: %.2f..%.2f, maior variacao por frame %.4f\n", minEsc, maxEsc, maxDEsc);
  printf("  luminancia percebida em susto: variacao %.0f%%, maior salto entre frames %.1f%%\n", 100 * (lmax - lmin) / lmin, 100 * piorSaltoL);
  VERIFICA(maxDEsc < 0.01f, "Respiracao continua (sem saltos de tamanho)");
  VERIFICA(vis.ritmo > 25, "Ritmo respiratorio acelerou no susto");
  VERIFICA(piorSaltoL < 0.05f, "Sem saltos de brilho > 5% entre frames");

  // 3b. Conversao OKLCh ida-e-volta preserva todas as cores da tabela
  { float pior = 0;
    for (uint8_t e = 0; e < (uint8_t)Emocao::TOTAL; e++) {
      float rgb[3] = {(float)EXPRESSOES[e].r, (float)EXPRESSOES[e].g, (float)EXPRESSOES[e].b}, lch[3], volta[3];
      rgbParaOklch(rgb, lch); oklchParaRgb(lch, volta);
      for (int k = 0; k < 3; k++) pior = max(pior, fabsf(volta[k] - rgb[k]));
    }
    printf("  erro maximo OKLCh ida-e-volta: %.3f niveis (de 255)\n", pior);
    VERIFICA(pior < 0.6f, "Conversao de cor perceptual exata (< 1 nivel)"); }

  // 4. Comportamento: abanar -> SUSTO; 3 abanoes -> RAIVA
  pedirEmocao("auto"); emo.manualAtiva = false; avancar(4000);
  auto abanar = [] { simQmi.ax = 1.6f; avancar(30); simQmi.ax = -1.2f; avancar(30); simQmi.ax = 0; avancar(900); };
  abanar();
  VERIFICA(emo.atual == Emocao::SUSTO, "Abanar a placa -> SUSTO");
  exportarPico("susto (abanao real)", 600);
  abanar(); abanar();
  VERIFICA(emo.atual == Emocao::RAIVA, "3 abanoes em 10 s -> RAIVA");
  avancar(7000);

  // 5. Olhar segue a inclinacao
  pedirEmocao("espera");
  for (int k = 1; k <= 20; k++) { simQmi.ax = 0.6f * k / 20; avancar(20); }
  VERIFICA(emo.atual == Emocao::ESPERA, "Inclinar devagar NAO causa susto");
  avancar(400);
  VERIFICA(olhos.px > 0.5f, "Inclinar para +X -> pupilas rolam para +X");
  { pedirEmocao("alegria"); avancar(500); float b[NUM_LEDS] = {0}; desenharRosto(b, expressaoDe(emo.atual), false, millis());
    int acesos = 0; for (int i = 8; i < 32; i++) acesos += b[i] > 0.5f;
    VERIFICA(acesos >= 10, "Em ALEGRIA a inclinar, olhos abrem para mostrar o olhar"); pedirEmocao("espera"); }
  for (int k = 19; k >= 0; k--) { simQmi.ax = 0.6f * k / 20; avancar(20); }
  avancar(1500);

  // 6. Ultrassom: pessoa perto -> CURIOSO; > 8 s -> AMOR; muito perto -> vesgo
  pedirEmocao("auto"); emo.manualAtiva = false;
  distancia(150); avancar(400); distancia(45); avancar(500);
  VERIFICA(emo.atual == Emocao::CURIOSO, "Pessoa a 45 cm -> CURIOSO");
  for (int i = 0; i < 18; i++) { distancia(44 + (i % 2) * 3); avancar(500); }
  VERIFICA(emo.atual == Emocao::AMOR, "Pessoa presente > 8 s com energia -> AMOR");
  for (int i = 0; i < 4; i++) { distancia(8 + (i % 2) * 2); avancar(300); }
  VERIFICA(olhos.convergencia > 0.4f, "Pessoa a < 12 cm -> olhos convergem (vesgo)");
  distancia(999); avancar(500); distancia(999); avancar(3000);

  // 7. Objeto imovel (parede a 50 cm) deixa de contar como pessoa
  distancia(50); avancar(500);
  for (int i = 0; i < 100; i++) { distancia(50); avancar(500); }
  VERIFICA(!pessoaPresente(millis()), "Objeto imovel a 50 cm ignorado apos 45 s");
  distancia(999); avancar(1000);

  // 7b. Ultrassom desligado -> detecao automatica de ausencia (nao e erro)
  simUsLigado = false; avancar(2000);
  VERIFICA(!us.detectado && statusGeral() == StatusHardware::SUCESSO, "HC-SR04 desligado -> 'ausente', sem marcar ERRO");
  simUsLigado = true; avancar(2500);
  VERIFICA(us.detectado, "HC-SR04 religado -> detetado de novo");

  // 8. Wi-Fi perdido -> CONFUSO + aviso; volta -> ALEGRIA (alivio)
  WiFi.estado = 0; avancar(6500);
  VERIFICA(rede.estado == EstadoRede::PERDIDO && emo.atual == Emocao::CONFUSO, "Wi-Fi perdido > 5 s -> CONFUSO");
  VERIFICA(vis.avisoAtivo, "Icone Wi-Fi aparece quando o problema comeca");
  WiFi.estado = WL_CONNECTED; avancar(1000);
  VERIFICA(emo.atual == Emocao::ALEGRIA, "Wi-Fi recuperado -> ALEGRIA transitoria (alivio)");
  avancar(3000);

  // 9. Falha do IMU -> ERRO (olhos X vermelhos) e recuperacao automatica
  simQmi.presente = false; avancar(3000);
  VERIFICA(imu.status == StatusHardware::ERRO, "IMU desligado -> ERRO apos 3 falhas");
  exportarPico("erro IMU (olhos X)", 1000);
  simQmi.presente = true; avancar(9000);
  VERIFICA(imu.status == StatusHardware::SUCESSO, "IMU religado -> recuperacao com backoff");

  // 10. Sede: solo seco
  simSoloMv = 2700; avancar(5000);
  VERIFICA(solo.comSede && emo.atual == Emocao::SEDE, "Solo seco (<30%) -> SEDE");
  simSoloMv = 1800; avancar(5000);
  VERIFICA(!solo.comSede, "Solo humido (>40%) -> sai de SEDE (histerese)");

  // 11. Inatividade -> ADORMECER -> PROTETOR; interacao -> ACORDAR
  avancar(TEMPO_PROTETOR_MS + 4000);
  VERIFICA(vis.modo == ModoEcra::PROTETOR, "Inatividade > 90 s -> protetor de ecra");
  simQmi.ax = 0.3f; avancar(60); simQmi.ax = 0; avancar(100);
  VERIFICA(vis.modo == ModoEcra::ACORDAR, "Mexer na planta -> acorda");
  avancar(2000);

  // 11b. SINALIZACAO DE SISTEMA (canal proprio)
  { const int IND = INDICADOR_Y * LARGURA + INDICADOR_X;
    auto ind = [&]() { return (int)espelho[IND][0] + espelho[IND][1] + espelho[IND][2]; };
    // (a) o indicador nunca colide com um rosto
    bool livre = true;
    for (uint8_t e = 0; e < (uint8_t)Emocao::TOTAL; e++) for (int x = 0; x < 2; x++) {
      float b[NUM_LEDS] = {0}; desenharRosto(b, EXPRESSOES[e], x == 1, millis()); if (b[IND] > 0) livre = false; }
    VERIFICA(livre, "Pixel do indicador nao e usado por nenhum rosto (9 emocoes + olhos X)");
    // (b) tudo bem -> indicador apagado
    pedirEmocao("espera"); avancar(1500);
    VERIFICA(nivelGeral() == Nivel::OK && ind() == 0, "Tudo bem -> indicador apagado (sem noticias = boas noticias)");
    paletaAtual = Paleta::OCEANO; olhos.tProximaSacada = millis() + 100000; avancar(5000); exportar("sinal:tudo bem (apagado)"); paletaAtual = Paleta::MANGUE;
    // (c) sensores opcionais ausentes nao acendem nada
    simUsLigado = false; avancar(2500);
    VERIFICA(subs[(uint8_t)Sub::ULTRASSOM].saude == Saude::AUSENTE && ind() == 0, "HC-SR04 ausente -> 'nao instalado', indicador apagado");
    simUsLigado = true; avancar(2500);
    // (d) erro do IMU: rosto mantem a cor da paleta; indicador vermelho FIXO
    paletaAtual = Paleta::OCEANO; pedirEmocao("espera"); avancar(6000);
    simQmi.presente = false; avancar(3000);
    VERIFICA(painel.ativo && painel.destaque == (int)Sub::IMU, "Erro do IMU -> painel abre primeiro (indicador escondido enquanto o painel mostra tudo)");
    avancar(6000);                          // painel fecha; fica o sinal periferico
    int mn = 999, mx = 0; bool soVermelho = true;
    for (int f = 0; f < 200; f++) { avancar(10); int v = espelho[IND][0]; mn = min(mn, v); mx = max(mx, v); if (espelho[IND][1] || espelho[IND][2]) soVermelho = false; }
    printf("  indicador em erro: vermelho %d..%d (fixo)\n", mn, mx);
    exportar("sinal:erro IMU (vermelho fixo)");
    VERIFICA(nivelGeral() == Nivel::ERRO && soVermelho && mn > 0 && mx - mn <= 1, "Erro do IMU -> indicador vermelho fixo (nao pisca)");
    { float alvo[3]; corEmocao(emo.atual, alvo); int ip = -1; float m = 0;
      for (int i = 0; i < NUM_LEDS; i++) { if (i == IND) continue; float s0 = espelho[i][0] + espelho[i][1] + espelho[i][2]; if (s0 > m) { m = s0; ip = i; } }
      float sa = alvo[0] + alvo[1] + alvo[2], err = 0; for (int k = 0; k < 3; k++) err = max(err, fabsf(espelho[ip][k] / m - alvo[k] / sa));
      printf("  rosto durante erro do IMU: cromaticidade vs paleta, erro %.3f\n", err);
      VERIFICA(err < 0.06f, "Erro do IMU NAO muda a cor do rosto (continua na paleta)"); }
    simQmi.presente = true; avancar(9000);
    VERIFICA(ind() == 0, "IMU recuperado -> indicador apaga sozinho");
    // (e) Wi-Fi: debounce, reacao breve, icone 1x, relembrar so a quem chega
    WiFi.estado = 0; avancar(3000);
    VERIFICA(ind() == 0 && emo.atual != Emocao::CONFUSO, "Queda de Wi-Fi < 5 s nao alarma (debounce)");
    exportar("sinal:queda curta < 5 s (nada)");
    WiFi.estado = WL_CONNECTED; avancar(2000);
    VERIFICA(!emo.transitoriaAtiva, "Queda curta recuperada sem 'alivio' falso");
    WiFi.estado = 0; avancar(6000);
    VERIFICA(emo.atual == Emocao::CONFUSO && vis.avisoAtivo, "Queda > 5 s -> CONFUSO + icone Wi-Fi (uma vez)");
    int ambar = 0, mnA = 999, mxA = 0;
    for (int f = 0; f < 400; f++) { avancar(10); int r = espelho[IND][0], g = espelho[IND][1]; if (r > g && g > 0 && !espelho[IND][2]) ambar++; mnA = min(mnA, r); mxA = max(mxA, r); }
    VERIFICA(ambar > 300 && mxA > mnA, "Wi-Fi em aviso -> indicador ambar a pulsar devagar");
    avancar(4000); { int g = 0; while ((millis() % PERIODO_PULSO_IND_MS) != PERIODO_PULSO_IND_MS / 2 && g++ < 5000) avancar(1); }
    exportar("sinal:wifi perdido (ambar a pulsar)");
    avancar(6000);
    VERIFICA(emo.atual != Emocao::CONFUSO, "Reage e acalma: CONFUSO termina (sem ficar 'preso')");
    bool repetiu = false; for (int k = 0; k < 40; k++) { avancar(1000); if (vis.avisoAtivo) repetiu = true; }
    VERIFICA(!repetiu, "Sem ninguem a olhar, o icone NAO se repete (40 s)");
    avancar(20000); registarInteracao(millis()); avancar(300);
    VERIFICA(vis.avisoAtivo, "Alguem chega (>60 s depois) -> icone relembrado");
    WiFi.estado = WL_CONNECTED; avancar(1500);
    VERIFICA(emo.atual == Emocao::ALEGRIA && ind() == 0, "Wi-Fi de volta -> alivio (ALEGRIA) e indicador apagado");
    rotaSaude();
    { std::ofstream j(std::string(pasta) + "/saude.json"); j << server.ultimaResposta.c_str(); }
    VERIFICA(strstr(server.ultimaResposta.c_str(), "\"sub\":\"wifi\",\"de\":\"aviso\",\"para\":\"ok\"") != nullptr,
             "Historico regista a queda e a recuperacao do Wi-Fi");
    paletaAtual = Paleta::MANGUE; avancar(4000);
  }

  // 11c. PAINEL DE ESTADO + BOTAO BOOT
  { auto iconeCor = [&](int sub) { int px = ORIGEM_ICONE[sub][1] * 8 + ORIGEM_ICONE[sub][0] + 1;   // pixel do topo-meio (ou primeiro aceso)
      for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) if (ICONES_SUB[sub][r] & (0b100 >> c)) { px = (ORIGEM_ICONE[sub][1] + r) * 8 + ORIGEM_ICONE[sub][0] + c; goto achou; }
      achou: return std::array<int,3>{espelho[px][0], espelho[px][1], espelho[px][2]}; };
    // icones nao se sobrepoem e cabem nos 4 quadrantes
    bool ok = true; int uso[NUM_LEDS] = {0};
    for (int i = 0; i < (int)Sub::TOTAL; i++) for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++)
      if (ICONES_SUB[i][r] & (0b100 >> c)) { int px = (ORIGEM_ICONE[i][1] + r) * 8 + ORIGEM_ICONE[i][0] + c; if (uso[px]++) ok = false; }
    VERIFICA(ok, "Quatro icones em quadrantes separados, sem sobreposicao");
    // toque curto no BOOT -> painel; cores = estado
    pedirEmocao("espera"); avancar(2000);
    simBotao = LOW; avancar(200); simBotao = HIGH; avancar(1500);
    auto w = iconeCor((int)Sub::WIFI), im = iconeCor((int)Sub::IMU);
    VERIFICA(painel.ativo && w[1] > w[0] && im[1] > im[0], "Toque curto no BOOT -> painel com icones verdes (OK)");
    exportar("painel:toque no BOOT (tudo ok)");
    simBotao = LOW; avancar(200); simBotao = HIGH; avancar(600);
    VERIFICA(!painel.ativo, "Segundo toque fecha o painel");
    // sensor nao instalado -> cinzento, sem destaque, sem alarme
    simUsLigado = false; avancar(2500);
    simBotao = LOW; avancar(200); simBotao = HIGH; avancar(1500);
    auto us_ = iconeCor((int)Sub::ULTRASSOM);
    VERIFICA(us_[0] == us_[1] && us_[1] == us_[2] && us_[0] > 0, "Sensor nao instalado -> icone cinzento (neutro, nao alarma)");
    exportar("painel:ultrassom nao instalado (cinzento)");
    simUsLigado = true; avancar(7000);
    // erro novo -> painel aparece SOZINHO com o culpado em destaque
    simQmi.presente = false; avancar(2600);
    auto im2 = iconeCor((int)Sub::IMU), w2 = iconeCor((int)Sub::WIFI);
    printf("  painel automatico no erro do IMU: IMU #%02X%02X%02X, Wi-Fi (atenuado) #%02X%02X%02X\n", im2[0], im2[1], im2[2], w2[0], w2[1], w2[2]);
    VERIFICA(painel.ativo && painel.destaque == (int)Sub::IMU && im2[0] > 0 && im2[1] == 0 && w2[1] < im2[0],
             "Erro novo -> painel abre sozinho, icone culpado vermelho e os outros atenuados");
    exportar("painel:erro no acelerometro (destaque)");
    avancar(6000);
    VERIFICA(!painel.ativo, "Painel automatico fecha sozinho (5 s)");
    avancar(70000); registarInteracao(millis()); avancar(800);
    VERIFICA(painel.ativo && painel.destaque == (int)Sub::IMU, "Erro persistente: painel relembrado quando alguem interage (>60 s)");
    simQmi.presente = true; avancar(12000);
    // manter o BOOT: anel de progresso; largar antes cancela
    int servidorParagens = server.paragens;
    simBotao = LOW; avancar(2900);
    int anel = 0; for (int k = 0; k < 28; k++) { uint8_t x, y; posicaoMoldura(k, x, y); int i = y * 8 + x;
      float r = espelho[i][0], g = espelho[i][1], b = espelho[i][2];
      if (b > 0 && fabsf(r / b - 160.0f / 255) < 0.1f && fabsf(g / b - 190.0f / 255) < 0.1f) anel++; }
    printf("  anel aos 2,9 s: %d de 28 pixels\n", anel);
    VERIFICA(anel >= 13 && anel <= 15, "Segurar o BOOT -> anel de progresso enche na moldura (metade aos 2,9 s)");
    exportar("painel:segurar BOOT (anel 2,9 s)");
    simBotao = HIGH; avancar(500);
    VERIFICA(wm.pedidosPortal == 0 && !painel.ativo, "Largar antes dos 5 s cancela (nada acontece)");
    simBotao = LOW; avancar(5200);
    VERIFICA(wm.pedidosPortal == 1 && wm.timeout == PORTAL_TIMEOUT_S && server.paragens == servidorParagens + 1 && !rede.servidorAtivo,
             "Segurar 5 s -> abre portal Wi-Fi (servidor liberta a porta 80, timeout 3 min)");
    simBotao = HIGH; avancar(1500);
    VERIFICA(subs[(uint8_t)Sub::WIFI].saude == Saude::AVISO, "Portal aberto -> Wi-Fi em 'atencao' (indicador ambar)");
    wm.portal = false; avancar(1500);
    VERIFICA(rede.servidorAtivo && subs[(uint8_t)Sub::WIFI].saude == Saude::OK, "Portal fechado -> servidor web volta sozinho");
    // pagina: botao 'Mostrar na placa'
    rotaPainel(); avancar(500);
    VERIFICA(painel.ativo, "Botao 'Mostrar na placa' (API) abre o painel");
    avancar(7000);
  }

  // 11d. ROTACAO AUTOMATICA DO ROSTO
  { auto gravidade = [](float phiGraus, float vertical = 1.0f) {        // chao a phi graus (0 = baixo do rosto)
      float f = phiGraus * PI / 180.0f;                                  // g no plano = (-sin f, cos f)
      simQmi.ax = -sinf(f) * vertical; simQmi.ay = cosf(f) * vertical; simQmi.az = sqrtf(max(0.0f, 1 - vertical * vertical)); };
    auto fisico = [](int x, int y, uint8_t* c) { matriz.corFisica(y * 8 + x, c); };
    pedirEmocao("espera"); olhos.tProximaSacada = millis() + 1000000;
    // placa de pe, posicao normal
    gravidade(0); avancar(3000);
    VERIFICA(rot.vertical && rot.quarto == 0 && fabsf(rot.anguloExibido) < 0.01f, "Placa de pe na posicao normal -> rosto a 0 graus");
    // girar 90 graus (chao a esquerda): debounce + animacao
    gravidade(90); avancar(300);
    VERIFICA(rot.quarto == 0, "Antes de 0,4 s estavel, ainda nao roda (debounce)");
    { unsigned long t0 = millis() - 300; while (rot.quarto != 1 && millis() - t0 < 2000) avancar(1);
      unsigned long dt = millis() - t0;
      printf("  tempo ate comecar a girar: %lu ms (filtro de gravidade + 400 ms de estabilidade)\n", dt);
      VERIFICA(rot.quarto == 1 && rot.animando && dt >= 400 && dt <= 700, "Chao a esquerda -> comeca a girar para 90 graus em 0,4-0,7 s"); }
    float maxPasso = 0, antes = rot.anguloExibido;
    for (int f = 0; f < 60; f++) { avancar(10); maxPasso = max(maxPasso, fabsf(rot.anguloExibido - antes)); antes = rot.anguloExibido; }
    printf("  giro animado: maior passo por frame %.1f graus\n", maxPasso);
    VERIFICA(!rot.animando && fabsf(rot.anguloExibido - 90) < 0.01f && maxPasso < 8.0f, "Giro animado suave termina exatamente em 90 graus");

    // mapeamento fisico exato: LED fisico (x,y) = vista (y, 7-x)
    bool mapa = true;
    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) { uint8_t c[3]; fisico(x, y, c); int i = (7 - x) * 8 + y;
      for (int k = 0; k < 3; k++) if (abs(c[k] - espelho[i][k]) > 1) mapa = false; }
    VERIFICA(mapa, "LEDs fisicos = vista rodada 90 graus (permutacao exata, boca do lado do chao)");
    // repouso recalibrado -> nao fica 'em movimento' (pode dormir)
    avancar(2000);
    VERIFICA(imu.calibrado && !mov.emMovimento, "Nova posicao vira o novo repouso (olhar centrado, sono possivel)");
    // olhar no referencial do rosto: chao desce um pouco para baixo-fisico -> pupila vai para +x do rosto
    simQmi.ax = -0.95f; simQmi.ay = 0.31f; avancar(1500);
    printf("  olhar com rosto a 90 graus: pupila (%.2f, %.2f)\n", olhos.px, olhos.py);
    VERIFICA(olhos.px > 0.3f && rot.quarto == 1, "Olhar segue a gravidade no referencial do rosto rodado");
    // janela de 15 graus e histerese
    gravidade(78); avancar(1500);
    VERIFICA(rot.quarto == 1, "A 12 graus do lado: mantem (dentro da janela)");
    gravidade(40); avancar(1500);
    VERIFICA(rot.quarto == 1, "A 40 graus (entre janelas): mantem a ultima orientacao (histerese)");
    gravidade(10); avancar(1500);
    VERIFICA(rot.quarto == 0, "A 10 graus do lado de baixo: volta a 0 graus");
    // 180 e 270 graus
    gravidade(180); avancar(1500);
    VERIFICA(rot.quarto == 2, "De cabeca para baixo -> 180 graus");

    { uint8_t c[3]; bool m = true; for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) { fisico(x, y, c); int i = (7 - y) * 8 + (7 - x);
        for (int k = 0; k < 3; k++) if (abs(c[k] - espelho[i][k]) > 1) m = false; }
      VERIFICA(m, "180 graus: LED fisico (x,y) = vista (7-x, 7-y)"); }
    gravidade(270); avancar(1500);
    VERIFICA(rot.quarto == 3, "Chao a direita -> 270 graus");

    // movimento breve nao roda
    gravidade(0); avancar(1500);
    gravidade(90); avancar(250); gravidade(0); avancar(1500);
    VERIFICA(rot.quarto == 0, "Inclinacao breve (< 0,4 s) nao roda o rosto");
    // placa deitada: nunca roda
    simQmi.ax = 0.5f; simQmi.ay = 0; simQmi.az = 0.87f; avancar(1500);
    VERIFICA(!rot.vertical && rot.quarto == 0, "Placa deitada (inclinada 30 graus) -> nao roda");
    // desligar na pagina
    gravidade(90); avancar(1500);
    server.argNome = String(""); server.argDur = String(""); 
    { auto ra = [](const char* v) { server.argNome = String(v); rotaRotacao(); };
      ra("2"); VERIFICA(server.ultimoCodigo == 400, "API rejeita auto invalido");
      ra("0"); avancar(800);
      VERIFICA(!rot.automatica && rot.quarto == 1 && preferencias.getUChar("rotfix", 9) == 1, "Fixar na pagina -> fica na posicao ATUAL (90 graus), como o bloqueio do telemovel");
      gravidade(180); avancar(1500);
      VERIFICA(rot.quarto == 1, "Com rotacao fixa, girar a placa nao muda o rosto");
      rotaEstado(); VERIFICA(strstr(server.ultimaResposta.c_str(), "\"rot\":{\"auto\":false") != nullptr, "Estado da rotacao na API");
      ra("1"); avancar(1500);
      VERIFICA(rot.quarto == 2, "Religar -> roda logo para a posicao atual (180 graus)"); }
    // Giro REAL (a mao, 90 graus em 0,4 s) nao deve assustar a planta
    { auto girar = [&](float de, float para, int ms) { for (int k = 1; k <= ms / 10; k++) { gravidade(de + (para - de) * k / (ms / 10.0f)); avancar(10); } };
      gravidade(0); avancar(3000); emo.transitoriaAtiva = false; pedirEmocao("auto"); emo.manualAtiva = false; avancar(500);
      float pico = 0; Emocao antesE = emo.atual;
      for (int k = 1; k <= 40; k++) { gravidade(90.0f * k / 40); avancar(10); pico = max(pico, imu.agitacao); }
      avancar(1500);
      printf("  giro a mao (90 graus/0,4 s): agitacao maxima %.2f g (susto a partir de %.2f g)\n", pico, LIMIAR_SUSTO_G);
      VERIFICA(emo.atual != Emocao::SUSTO && rot.quarto == 1, "Girar a placa a mao roda o rosto SEM causar susto");
      (void)antesE;
      pedirEmocao("alegria"); olhos.tProximaSacada = millis() + 1000000; avancar(4000); exportar("rot:90 graus");
      girar(90, 180, 400); avancar(4000); exportar("rot:180 graus");
      girar(180, 270, 400); avancar(4000); exportar("rot:270 graus");
      girar(270, 360, 400); avancar(4000); exportar("rot:0 graus");
      VERIFICA(rot.quarto == 0, "Volta completa (360 graus) termina na posicao normal");
    }
    // repor posicao deitada original dos outros testes
    gravidade(0); avancar(1500);
    simQmi.ax = 0; simQmi.ay = 0; simQmi.az = 1; avancar(3000);
  }

  // 11e. MONTAGEM DO SENSOR + CALIBRACAO GUIADA
  { auto chao = [](float phiGraus) { float f = phiGraus * PI / 180.0f; simQmi.ax = -sinf(f); simQmi.ay = cosf(f); simQmi.az = 0; };
    auto confere4 = [&](const char* rotulo) {      // nas 4 posicoes reais, o rosto fica direito?
      bool ok = true; char det[64] = "";
      for (int q = 0; q < 4; q++) { chao(q * 90.0f); avancar(1600); if (rot.quarto != q) { ok = false; snprintf(det, 64, " (real %d -> rosto %d)", q * 90, rot.quarto * 90); } }
      chao(0); avancar(1600); return std::make_pair(ok, std::string(rotulo) + det); };
    auto calib = [](const char* passo) { server.argNome = String(passo); rotaCalibrarOrientacao(); return std::string(server.ultimaResposta.c_str()); };
    pedirEmocao("espera"); olhos.tProximaSacada = millis() + 1000000;
    // (a) reproduzir o defeito relatado: sem correcao, a placa real fica sempre 90 graus anti-horario
    aplicarMontagem(0, false, millis());
    chao(0); avancar(2000);
    printf("  sem correcao, placa real direita -> rosto a %d graus (anti-horario = 270)\n", rot.quarto * 90);
    VERIFICA(rot.quarto == 3, "Reproduz o defeito: sem correcao o rosto fica 90 graus anti-horario");
    // (b) correcao de fabrica (90 graus) acerta as 4 posicoes
    aplicarMontagem(CORRECAO_EIXOS_PADRAO, ESPELHO_EIXOS_PADRAO, millis());
    { auto r = confere4("Correcao de fabrica (+90 graus): rosto direito nas 4 posicoes"); VERIFICA(r.first, r.second.c_str()); }
    // (c) calibracao guiada a partir de uma montagem errada
    aplicarMontagem(0, false, millis()); avancar(500);
    chao(0); avancar(1500); std::string r1 = calib("1");
    chao(90); avancar(1500); std::string r2 = calib("2");
    printf("  calibracao: %s\n", r2.c_str());
    VERIFICA(eixos.quarto == 1 && !eixos.espelho && preferencias.getUChar("eixq", 9) == 1, "Calibracao em 2 passos encontra a montagem real (90 graus) e guarda-a");
    { auto r = confere4("Depois de calibrar: rosto direito nas 4 posicoes"); VERIFICA(r.first, r.second.c_str()); }
    // (d) placa ESPELHADA: o erro alterna de sentido; a calibracao deteta
    simQmi.montagemEspelho = true; simQmi.montagemQuarto = 1;
    aplicarMontagem(1, false, millis()); avancar(500);
    { int erros[4]; for (int q = 0; q < 4; q++) { chao(q * 90.0f); avancar(1600); erros[q] = ((rot.quarto - q) % 4 + 4) % 4; }
      printf("  placa espelhada sem calibrar: erro por posicao (quartos) %d %d %d %d\n", erros[0], erros[1], erros[2], erros[3]); }
    chao(0); avancar(1500); calib("1"); chao(90); avancar(1500); r2 = calib("2");
    printf("  calibracao (espelhada): %s\n", r2.c_str());
    VERIFICA(eixos.espelho, "Montagem espelhada detetada pela calibracao");
    { auto r = confere4("Espelhada calibrada: rosto direito nas 4 posicoes"); VERIFICA(r.first, r.second.c_str()); }
    // (e) montagem a 180 graus
    simQmi.montagemEspelho = false; simQmi.montagemQuarto = 2;
    chao(0); avancar(1500); calib("1"); chao(90); avancar(1500); calib("2");
    { auto r = confere4("Montagem a 180 graus calibrada: rosto direito nas 4 posicoes"); VERIFICA(r.first && eixos.quarto == 2, r.second.c_str()); }
    // (f) protecoes do assistente
    simQmi.ax = 0.2f; simQmi.ay = 0; simQmi.az = 0.98f; avancar(1500);
    std::string rf = calib("1");
    VERIFICA(server.ultimoCodigo == 409 && rf.find("vertical") != std::string::npos, "Calibrar com a placa deitada -> pede para a pôr de pe (409)");
    chao(0); avancar(1500); calib("1"); chao(180); avancar(1500); rf = calib("2");
    VERIFICA(server.ultimoCodigo == 409 && rf.find("Recomece") != std::string::npos, "Passo 2 que nao esta a 90 graus -> pede para recomecar");
    calib("2"); VERIFICA(server.ultimoCodigo == 409, "Passo 2 sem passo 1 -> recusado");
    calib("9"); VERIFICA(server.ultimoCodigo == 400, "Passo invalido -> 400");
    // (g) repor de fabrica + voltar a placa real
    simQmi.montagemQuarto = 1; simQmi.montagemEspelho = false;
    calib("repor"); VERIFICA(eixos.quarto == CORRECAO_EIXOS_PADRAO && !eixos.espelho, "Repor montagem de fabrica");
    { auto r = confere4("Placa real com montagem de fabrica: rosto direito nas 4 posicoes"); VERIFICA(r.first, r.second.c_str()); }
    // (h) olhar no sentido certo na placa real (placa deitada, inclinar para a direita)
    simQmi.ax = 0; simQmi.ay = 0; simQmi.az = 1; avancar(3000);
    for (int k = 1; k <= 20; k++) { simQmi.ax = 0.6f * k / 20; avancar(20); } avancar(800);
    VERIFICA(olhos.px > 0.4f, "Placa real: inclinar para a direita -> pupilas para a direita");
    for (int k = 19; k >= 0; k--) { simQmi.ax = 0.6f * k / 20; avancar(20); } avancar(2000);
    rotaEstado(); VERIFICA(strstr(server.ultimaResposta.c_str(), "\"eixos\":90,\"espelho\":false") != nullptr, "API mostra a montagem do sensor");
  }

  // 11f. GIRAR E FIXAR (pagina e BOOT)
  { auto chao = [](float phiGraus) { float f = phiGraus * PI / 180.0f; simQmi.ax = -sinf(f); simQmi.ay = cosf(f); simQmi.az = 0; };
    auto girarApi = [](const char* v) { server.argNome = String(v); rotaGirar(); };
    auto toque = [](int ms = 120) { simBotao = LOW; avancar(ms); simBotao = HIGH; };
    pedirEmocao("espera"); olhos.tProximaSacada = millis() + 1000000;
    { server.argNome = String("1"); rotaRotacao(); }
    // (a) "rodar ate ficar direito": placa real com montagem errada, em modo automatico
    aplicarMontagem(0, false, millis()); chao(0); avancar(2000);
    VERIFICA(rot.quarto == 3, "Montagem errada: placa direita mostra o rosto torto (270)");
    girarApi("1"); avancar(700);
    VERIFICA(rot.quarto == 0 && eixos.quarto == 1 && preferencias.getUChar("eixq", 9) == 1, "Um toque em girar endireita JA o rosto e aprende a correcao (guardada)");
    { bool ok = true; for (int q = 0; q < 4; q++) { chao(q * 90.0f); avancar(1600); if (rot.quarto != q) ok = false; } chao(0); avancar(1600);
      VERIFICA(ok, "Depois de girar uma vez, a rotacao automatica acerta as 4 posicoes"); }
    girarApi("-1"); avancar(700); VERIFICA(rot.quarto == 3, "Girar anti-horario desfaz (simetria)");
    girarApi("1"); avancar(700);
    girarApi("x"); VERIFICA(server.ultimoCodigo == 400, "API rejeita sentido invalido");
    // (b) modo fixo: girar escolhe a posicao, e fica
    { server.argNome = String("0"); rotaRotacao(); } avancar(300);
    girarApi("1"); avancar(700);
    VERIFICA(!rot.automatica && rot.quarto == 1 && preferencias.getUChar("rotfix", 9) == 1, "Fixo: girar muda a posicao fixa (guardada)");
    chao(180); avancar(1600); VERIFICA(rot.quarto == 1, "Fixo: mexer na placa nao muda o rosto");
    chao(0); avancar(1600);
    { server.argNome = String("1"); rotaRotacao(); } avancar(1600);
    VERIFICA(rot.automatica && rot.quarto == 0, "Voltar a automatica -> segue a placa outra vez");
    // (c) BOOT: duplo toque entra no modo girar; toque gira; segurar 1,5 s fixa
    toque(); avancar(150); toque(); avancar(200);
    VERIFICA(botao.modoGirar && !painel.ativo, "Duplo toque no BOOT -> modo girar (sem abrir o painel)");
    { int ic = 0; for (int i = 0; i < NUM_LEDS; i++) ic += espelho[i][2] > espelho[i][0]; exportar("girar:duplo toque (icone girar)"); VERIFICA(ic > 10, "Modo girar mostra o icone de girar"); }
    avancar(1500);
    { int cantos = (espelho[0][2] > 0) + (espelho[7][2] > 0) + (espelho[56][2] > 0);
      exportar("girar:modo girar (cantos acesos)"); VERIFICA(cantos == 3, "Modo girar: cantos acesos indicam 'a espera do botao'"); }
    toque(); avancar(700);
    VERIFICA(rot.quarto == 1 && eixos.quarto == 2, "No modo girar, um toque gira 90 graus (imediato)");
    toque(); avancar(700); toque(); avancar(700); toque(); avancar(700);
    VERIFICA(rot.quarto == 0 && eixos.quarto == 1, "Quatro toques = volta completa");
    simBotao = LOW; avancar(900);
    { int anel = 0; for (int k = 0; k < 28; k++) { uint8_t x, y; posicaoMoldura(k, x, y); int i = y * 8 + x; anel += espelho[i][2] > espelho[i][0]; }
      exportar("girar:segurar para fixar (anel)"); VERIFICA(anel >= 10, "Segurar no modo girar mostra o anel (rapido)"); }
    avancar(700); simBotao = HIGH; avancar(100);
    VERIFICA(!rot.automatica && !botao.modoGirar && wm.pedidosPortal == 1, "Segurar 1,5 s fixa a orientacao e sai do modo (nao abre o Wi-Fi)");
    exportar("girar:fixado (cadeado)");
    toque(); avancar(150); toque(); avancar(300);
    simBotao = LOW; avancar(1700); simBotao = HIGH; avancar(300);
    VERIFICA(rot.automatica, "Repetir (duplo toque + segurar) solta o cadeado");
    // (d) saida por inatividade; toque simples continua a abrir o painel
    toque(); avancar(150); toque(); avancar(300);
    avancar(MODO_GIRAR_INATIVO_MS + 500);
    VERIFICA(!botao.modoGirar, "Modo girar sai sozinho apos 10 s sem tocar");
    toque(); avancar(600);
    VERIFICA(painel.ativo, "Toque simples continua a abrir o painel (apos a janela de 350 ms)");
    avancar(7000);
  }

  // 11g. PILHA E DIAGNOSTICO DE REINICIOS
  { printf("  show(): %d chamadas, %d fora do loop/setup\n", showsTotal, showForaDoLoop);
    VERIFICA(showsTotal > 1000 && showForaDoLoop == 0, "show() (usa ~6 KB de pilha na lib) e chamado so a partir do loop()/setup()");
    // crash simulado durante o desenho -> no arranque seguinte sabemos onde
    migalhas.magia = MAGIA_RTC; migalhas.etapa = (uint8_t)Etapa::RENDER; migalhas.crashesSeguidos = 0;
    simMotivoReinicio = ESP_RST_PANIC; analisarReinicio();
    VERIFICA(reinicio.foiCrash && reinicio.etapaCrash == Etapa::RENDER && reinicio.crashesSeguidos == 1,
             "Depois de um crash: motivo 'panic' e modulo 'desenho' identificados");
    migalhas.etapa = (uint8_t)Etapa::LEDS; analisarReinicio();
    VERIFICA(reinicio.crashesSeguidos == 2 && reinicio.etapaCrash == Etapa::LEDS, "Crashes seguidos sao contados (reinicio em ciclo detetavel)");
    simMotivoReinicio = ESP_RST_POWERON; analisarReinicio();
    VERIFICA(!reinicio.foiCrash && reinicio.crashesSeguidos == 0, "Ligar normalmente zera a contagem");
    migalhas.magia = 0xDEADBEEF; migalhas.etapa = 200; simMotivoReinicio = ESP_RST_PANIC; analisarReinicio();
    VERIFICA(migalhas.magia == MAGIA_RTC && reinicio.etapaCrash == Etapa::ARRANQUE, "RAM RTC com lixo (ligar a frio) e tratada com seguranca");
    simMotivoReinicio = ESP_RST_PANIC; migalhas.etapa = (uint8_t)Etapa::RENDER; migalhas.crashesSeguidos = 0; analisarReinicio();
    avancar(1500); rotaEstado();
    { std::ofstream j(std::string(pasta) + "/estado_crash.json"); j << server.ultimaResposta.c_str(); }
    VERIFICA(strstr(server.ultimaResposta.c_str(), "\"reinicio\":\"CRASH (panic)\",\"crash_em\":\"desenho\"") != nullptr,
             "API/pagina mostram o motivo do ultimo reinicio e o modulo");
    VERIFICA(strstr(server.ultimaResposta.c_str(), "\"pilha_livre\":9000") != nullptr, "API mostra a pilha livre minima medida");
    simMotivoReinicio = ESP_RST_POWERON; analisarReinicio();
  }

  // 12. API web
  pedirEmocao("xpto"); VERIFICA(server.ultimoCodigo == 400, "API rejeita emocao desconhecida (400)");
  { auto pedirBrilho = [](const char* v) { server.argNome = String(v); rotaBrilho(); };
    pedirBrilho("200"); VERIFICA(server.ultimoCodigo == 400, "API rejeita brilho fora de 5-100");
    pedirBrilho("25");  VERIFICA(brilhoGeralPct == 25 && preferencias.tem && preferencias.brilho == 25, "Brilho 25% aplicado e guardado na flash");
    pedirBrilho("40"); }
  { auto pal = [](const char* n, const char* c) { server.argNome = String(n); server.argDur = String(c); rotaPaleta(); };
    pal("arco_iris", ""); VERIFICA(server.ultimoCodigo == 400, "API rejeita paleta desconhecida");
    pal("cor_unica", "zz12"); VERIFICA(server.ultimoCodigo == 400, "API rejeita cor mal formatada");
    pal("cor_unica", "050505"); VERIFICA(server.ultimoCodigo == 400, "API rejeita cor quase preta (rosto sumiria)");
    { float piorPar = 9, piorRes = 9;
      float temaMin = 9;
      for (uint8_t q = 0; q < (uint8_t)Paleta::COR_UNICA; q++) {
        paletaAtual = (Paleta)q;
        float lab[9][3];
        for (uint8_t e = 0; e < 9; e++) { float rgb[3], lch[3]; corEmocao((Emocao)e, rgb); rgbParaOklch(rgb, lch);
          lab[e][0] = lch[0]; lab[e][1] = lch[1] * cosf(lch[2]); lab[e][2] = lch[1] * sinf(lch[2]); }
        auto dist = [](const float* a, const float* b) { return sqrtf((a[0]-b[0])*(a[0]-b[0]) + (a[1]-b[1])*(a[1]-b[1]) + (a[2]-b[2])*(a[2]-b[2])); };
        for (int a = 0; a < 9; a++) for (int b = a + 1; b < 9; b++) piorPar = min(piorPar, dist(lab[a], lab[b]));
        const uint8_t* res[2] = {COR_TENTANDO, COR_ERRO};
        for (int r = 0; r < 2; r++) { float rgb[3] = {(float)res[r][0], (float)res[r][1], (float)res[r][2]}, lch[3], l2[3];
          rgbParaOklch(rgb, lch); l2[0] = lch[0]; l2[1] = lch[1] * cosf(lch[2]); l2[2] = lch[1] * sinf(lch[2]);
          for (int e = 0; e < 9; e++) piorRes = min(piorRes, dist(lab[e], l2)); }
      }
      // tema: espera e alegria de cada paleta longe da original
      for (uint8_t q = 1; q < (uint8_t)Paleta::COR_UNICA; q++) for (uint8_t e = 0; e < 2; e++) {
        float a[3], b[3], la[3], lb[3]; paletaAtual = Paleta::MANGUE; corEmocao((Emocao)e, a); paletaAtual = (Paleta)q; corEmocao((Emocao)e, b);
        rgbParaOklch(a, la); rgbParaOklch(b, lb);
        float xa = la[1] * cosf(la[2]), ya = la[1] * sinf(la[2]), xb = lb[1] * cosf(lb[2]), yb = lb[1] * sinf(lb[2]);
        temaMin = min(temaMin, sqrtf((la[0]-lb[0])*(la[0]-lb[0]) + (xa-xb)*(xa-xb) + (ya-yb)*(ya-yb))); }
      paletaAtual = Paleta::MANGUE;
      printf("  tema: espera/alegria de cada paleta vs original dE>=%.3f\n", temaMin);
      VERIFICA(temaMin >= 0.149f, "Trocar de paleta muda visivelmente as emocoes mais frequentes");
      printf("  paletas: par mais parecido dE=%.3f, mais perto de ERRO/TENTANDO dE=%.3f\n", piorPar, piorRes);
      VERIFICA(piorPar >= 0.084f && piorRes >= 0.109f, "Todas as paletas: emocoes distinguiveis e longe das cores de hardware"); }
    pal("oceano", "");
    float c[3]; corEmocao(Emocao::ALEGRIA, c);
    VERIFICA(server.ultimoCodigo == 200 && server.ultimoCodigo == 200 && preferencias.paleta == 1, "Paleta oceano aplicada e guardada");
    pedirEmocao("alegria"); avancar(7000);
    { // a cor nos LEDs tem de convergir para a cor da paleta (comparar a cromaticidade, o brilho e outro)
      float alvo[3]; corEmocao(Emocao::ALEGRIA, alvo);
      float mx = 0; int ip = -1; for (int i = 0; i < NUM_LEDS; i++) { float s0 = espelho[i][0] + espelho[i][1] + espelho[i][2]; if (s0 > mx) { mx = s0; ip = i; } }
      float sa = alvo[0] + alvo[1] + alvo[2], erro = 0;
      for (int k = 0; k < 3; k++) erro = max(erro, fabsf(espelho[ip][k] / mx - alvo[k] / sa));
      printf("  cromaticidade nos LEDs vs paleta oceano/alegria: erro %.3f\n", erro);
      VERIFICA(erro < 0.05f, "LEDs mostram a cor da nova paleta"); }
    { rotaMatriz(); const char* t = server.ultimaResposta.c_str();
      VERIFICA(strlen(t) == 384, "/api/matriz devolve 64 pixels RRGGBB");
      int ip = 0; float mx = 0; for (int i = 0; i < NUM_LEDS; i++) { float s0 = espelho[i][0] + espelho[i][1] + espelho[i][2]; if (s0 > mx) { mx = s0; ip = i; } }
      char esperado[7]; snprintf(esperado, 7, "%02X%02X%02X", espelho[ip][0], espelho[ip][1], espelho[ip][2]);
      VERIFICA(!strncmp(t + ip * 6, esperado, 6), "Espelho da pagina = valores reais enviados aos LEDs"); }
    // Sensor de solo desligado (leitura saturada) NAO pode tapar a paleta
    simSoloMv = 10; avancar(4000);
    rotaEstado();
    VERIFICA(!solo.presente && subs[(uint8_t)Sub::SOLO].saude == Saude::AUSENTE && nivelGeral() == Nivel::OK,
             "Solo desligado -> 'nao instalado', nivel geral continua OK (nao e erro)");
    simSoloMv = 1800; avancar(4000);
    VERIFICA(solo.presente, "Solo religado -> detetado de novo");

    // Ordem das cores: a matriz FISICA desta placa e RGB
    { auto corLed = [](int i, uint8_t* c) { matriz.corFisica(i, c); };
      int ip = 0; float mx = 0; for (int i = 0; i < NUM_LEDS; i++) { float s0 = espelho[i][0] + espelho[i][1] + espelho[i][2]; if (s0 > mx) { mx = s0; ip = i; } }
      uint8_t f[3];
      server.argNome = String("GRB"); rotaOrdem(); avancar(50); corLed(indicePixel(ip % 8, ip / 8), f);
      printf("  ordem GRB (V7.4): espelho #%02X%02X%02X -> LED fisico #%02X%02X%02X\n", espelho[ip][0], espelho[ip][1], espelho[ip][2], f[0], f[1], f[2]);
      bool trocado = abs(f[0] - espelho[ip][1]) <= 1 && abs(f[1] - espelho[ip][0]) <= 1;
      VERIFICA(trocado, "Reproduz o defeito: com GRB o LED fisico troca vermelho e verde");
      server.argNome = String("RGB"); rotaOrdem(); avancar(50); corLed(indicePixel(ip % 8, ip / 8), f);
      printf("  ordem RGB (V7.5): espelho #%02X%02X%02X -> LED fisico #%02X%02X%02X\n", espelho[ip][0], espelho[ip][1], espelho[ip][2], f[0], f[1], f[2]);
      VERIFICA(abs(f[0] - espelho[ip][0]) <= 1 && abs(f[1] - espelho[ip][1]) <= 1 && abs(f[2] - espelho[ip][2]) <= 1 && preferencias.paleta >= 0,
               "Com RGB o LED fisico mostra a mesma cor que o espelho");
      // ERRO vermelho com a ordem errada aparecia VERDE (o sintoma relatado)
      uint8_t vermelho[3] = {200, 0, 0}, w[3];
      Adafruit_NeoPixel teste(1, 0, NEO_GRB); teste.setPixelColor(0, vermelho[0], vermelho[1], vermelho[2]); teste.corFisica(0, w);
      VERIFICA(w[1] == 200 && w[0] == 0, "Explica o sintoma: vermelho de ERRO enviado em GRB acende VERDE numa matriz RGB");
      // Teste de cores da Web: R, G, B puros
      rotaTesteCor(); bool okR, okG, okB;
      avancar(500);  corLed(0, f); okR = f[0] > 0 && f[1] == 0 && f[2] == 0;
      avancar(2000); corLed(0, f); okG = f[1] > 0 && f[0] == 0 && f[2] == 0;
      avancar(2000); corLed(0, f); okB = f[2] > 0 && f[0] == 0 && f[1] == 0;
      VERIFICA(okR && okG && okB, "Botao 'Testar cores' mostra vermelho, verde e azul puros");
      avancar(2500); VERIFICA(faseTesteCor(millis()) == -1, "Teste de cores termina sozinho (6 s)");
      server.argNome = String("XYZ"); rotaOrdem(); VERIFICA(server.ultimoCodigo == 400, "API rejeita ordem de cor invalida");
    }
    pal("cor_unica", "FF8800");
    bool todas = true; for (uint8_t e = 0; e < (uint8_t)Emocao::TOTAL; e++) { corEmocao((Emocao)e, c); todas &= (c[0] == 255 && c[1] == 136 && c[2] == 0); }
    VERIFICA(todas && preferencias.cor == 0xFF8800, "Cor unica #FF8800 em todas as emocoes e guardada");
    rotaEstado(); { std::ofstream j(std::string(pasta) + "/estado_paleta.json"); j << server.ultimaResposta.c_str(); }
    pal("mangue", ""); emo.manualAtiva = false; }
  rotaEstado();
  { std::ofstream j(std::string(pasta) + "/estado.json"); j << server.ultimaResposta.c_str(); }

  // 13. Energia baixa -> SONO
  pedirEmocao("auto"); emo.manualAtiva = false; emo.energia = 15; avancar(1500);
  VERIFICA(emo.atual == Emocao::SONO, "Energia < 20 -> SONO");
  emo.energia = 70;

  // ================= Exportação para pré-visualização =================
  for (uint8_t e = 0; e < (uint8_t)Emocao::TOTAL; e++) {
    pedirEmocao(EXPRESSOES[e].nome); olhos.alvoX = olhos.alvoY = 0; olhos.px = olhos.py = 0;
    olhos.tProximaSacada = millis() + 100000; olhos.tProximoPiscar = millis() + 100000;
    char et[64]; snprintf(et, 64, "rosto:%s", EXPRESSOES[e].nome); exportarPico(et, 1300);
  }
  for (uint8_t q = 0; q < (uint8_t)Paleta::COR_UNICA; q++) {
    paletaAtual = (Paleta)q;
    for (uint8_t e = 0; e < (uint8_t)Emocao::TOTAL; e++) {
      pedirEmocao(EXPRESSOES[e].nome); olhos.alvoX = olhos.alvoY = 0; olhos.px = olhos.py = 0;
      olhos.tProximaSacada = millis() + 100000;
      char et[64]; snprintf(et, 64, "pal:%s:%s", NOMES_PALETA[q], EXPRESSOES[e].nome); exportarPico(et, 1300);
    }
  }
  paletaAtual = Paleta::MANGUE;
  // Estados da API por paleta (para testar a pagina num navegador real)
  { auto dump = [&](const char* nome) {
      rotaEstado(); { std::ofstream j(std::string(pasta) + "/web_estado_" + nome + ".json"); j << server.ultimaResposta.c_str(); }
      rotaSaude();  { std::ofstream j(std::string(pasta) + "/web_saude_" + nome + ".json"); j << server.ultimaResposta.c_str(); }
      rotaMatriz(); { std::ofstream j(std::string(pasta) + "/web_matriz_" + nome + ".txt"); j << server.ultimaResposta.c_str(); } };
    corUnica = 0xFF8800;
    for (uint8_t q = 0; q < (uint8_t)Paleta::TOTAL; q++) {
      paletaAtual = (Paleta)q; pedirEmocao("espera"); olhos.tProximaSacada = millis() + 100000;
      avancar(9000);
      // capturar no pico da respiracao
      int g = 0; while (fabsf(vis.fase - 0.5f) > 0.02f && g++ < 20000) avancar(1);
      dump(NOMES_PALETA[q]);
    }
    paletaAtual = Paleta::OCEANO; simQmi.presente = false; avancar(4000); dump("erro_imu");
    { rotaSaude(); std::ofstream j(std::string(pasta) + "/web_saude_erro_imu.json"); j << server.ultimaResposta.c_str(); } simQmi.presente = true; avancar(9000);
    rotaTesteCor(); avancar(2500); dump("teste_verde"); avancar(4000);
    paletaAtual = Paleta::MANGUE; corUnica = COR_UNICA_PADRAO; }
  pedirEmocao("espera");
  struct { const char* n; float ax, ay; } incl[] = {{"olhar:esquerda", -0.7f, 0}, {"olhar:direita", 0.7f, 0},
                                                    {"olhar:cima", 0, -0.7f}, {"olhar:baixo", 0, 0.7f}};
  for (auto& o : incl) {                 // inclinacao gradual (400 ms), como na vida real
    for (int k = 1; k <= 20; k++) { simQmi.ax = o.ax * k / 20; simQmi.ay = o.ay * k / 20; avancar(20); }
    exportarPico(o.n, 1300);
    for (int k = 19; k >= 0; k--) { simQmi.ax = o.ax * k / 20; simQmi.ay = o.ay * k / 20; avancar(20); }
  }
  simQmi.ax = simQmi.ay = 0; avancar(1500);
  pedirEmocao("curioso"); for (int i = 0; i < 6; i++) { distancia(8); avancar(200); }
  exportarPico("olhar:vesgo (8 cm)", 1300);
  distancia(999); avancar(2000);
  pedirEmocao("espera"); avancar(2000);
  for (int k = 0; k < 2; k++) {   // respiracao: rosto cheio vs encolhido
    float alvo = k == 0 ? 0.0f : 0.5f; int g = 0;
    while (fabsf(vis.fase - alvo) > 0.02f && g++ < 20000) avancar(1);
    exportar(k == 0 ? "olhar:expira (menor)" : "olhar:inspira (cheio)");
  }

  wm.portal = true; avancar(3000);
  exportar("aviso:portal wifi");
  wm.portal = false; WiFi.estado = 0; avancar(5600 + 3200);
  exportar("aviso:wifi perdido (sinal fraco)");
  WiFi.estado = WL_CONNECTED; avancar(4000);

  rotaProtetor(); avancar(3000);
  const TipoProtetor tipos[3] = {TipoProtetor::MARE, TipoProtetor::VIDA, TipoProtetor::VAGALUMES};
  const char* nomes[3] = {"mare", "vida", "vagalumes"};
  for (int k = 0; k < 3; k++) {
    iniciarProtetor(tipos[k], millis()); avancar(1500);
    for (int f = 0; f < 40; f++) { avancar(100); char et[48]; snprintf(et, 48, "prot:%s %d", nomes[k], f); exportar(et); }
  }

  // Cena animada para GIF (rotulos 'gif:...'), 10 fps
  pedirEmocao("auto"); emo.manualAtiva = false; registarInteracao(millis()); avancar(2500);
  auto quadros = [](const char* rot, int n, std::function<void(int)> antes = nullptr) {
    for (int i = 0; i < n; i++) { if (antes) antes(i); avancar(100); char et[64]; snprintf(et, 64, "gif:%s", rot); exportar(et); }
  };
  pedirEmocao("espera", "8");
  quadros("respirar + olhar a volta", 80);
  emo.manualAtiva = false;
  quadros("inclinar", 30, [](int i) { simQmi.ax = 0.7f * sinf(i * 0.21f); simQmi.ay = 0.4f * cosf(i * 0.3f); });
  simQmi.ax = simQmi.ay = 0;
  quadros("abanao!", 25, [](int i) { if (i < 3) simQmi.ax = (i % 2) ? -1.5f : 1.6f; else simQmi.ax = 0; });
  quadros("pessoa perto", 30, [](int i) { distancia(i < 25 ? 40 + (i % 3) : 10); });
  distancia(999); avancar(500);
  quadros("wifi caiu", 35, [](int i) { WiFi.estado = 0; if (i == 0) { rede.tPerda = millis() - 6000; } (void)i; });
  WiFi.estado = WL_CONNECTED;
  quadros("wifi voltou", 15);
  rotaProtetor();
  quadros("protetor: mare", 40);
  saida.close();
}

int main(int argc, char** argv) {
  srand(7);
  testesV6();
  testesV7(argc > 1 ? argv[1] : ".");
  printf("\nResultado: %d falha(s)\n", falhas);
  return falhas ? 1 : 0;
}
