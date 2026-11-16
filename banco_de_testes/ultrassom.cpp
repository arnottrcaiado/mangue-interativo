// Cenários realistas do HC-SR04 (ruído, falhas de eco, leituras espúrias).
// Compilar com -DINO="\"caminho.ino\"" ; uso: ./ultrassom <cenario>
// Mede o comportamento da planta (não o algoritmo isolado): presença, sustos,
// emoções, olhar. Serve para comparar versões do firmware com o MESMO ruído.
#include INO
#include <random>
unsigned long simMillis = 0; int simSoloMv = 1800; int simBotao = HIGH;
esp_reset_reason_t simMotivoReinicio = ESP_RST_POWERON; unsigned simPilhaLivre = 9000;
SerialStub Serial; EspStub ESP; TwoWire Wire; SimQMI simQmi; WiFiStub WiFi; MDNSStub MDNS;

// ---- modelo do sensor
static std::mt19937 rng(12345);
static float sigmaCm = 1.2f, pFalha = 0.06f, pEspuria = 0.03f;
static float (*verdade)(float tSeg) = nullptr;            // distância real (>= 999 = nada)
static float tCenario0 = 0;
static unsigned long leituraSimulada() {
  float d = verdade((millis() - tCenario0) / 1000.0f);
  std::uniform_real_distribution<float> u(0, 1);
  std::normal_distribution<float> n(0, sigmaCm);
  float r = u(rng);
  if (d >= 999 || r < pFalha) return 38000UL;                       // sem eco (timeout do módulo)
  if (r < pFalha + pEspuria) {                                       // eco espúrio (lóbulo lateral / cruzado)
    float e = u(rng) < 0.6f ? 3 + u(rng) * 22 : 100 + u(rng) * 200;
    return (unsigned long)(e * 58);
  }
  return (unsigned long)(max(2.5f, d + n(rng)) * 58);
}
static void avancar(unsigned long ms);
// ---- métricas
struct Metricas { int trocasPresenca = 0, sustos = 0, trocasEmocao = 0, interacoes = 0, trocasVesgo = 0;
                  long tAmorMs = -1, tPresencaMs = -1, tFimPresencaMs = -1, tSustoMs = -1;
                  long tReageMs = -1; float proxMin = 9, proxMax = -1, dProxMax = 0, proxAnt = -1; } M;
static bool presAnt = false, vesgoAnt = false; static Emocao emoAnt; static unsigned long tSustoAnt = 0, tInterAnt = 0;
static void medir() {
  bool p = pessoaPresente(millis());
  long t = (long)(millis() - tCenario0);
  if (p != presAnt) { M.trocasPresenca++; if (p && M.tPresencaMs < 0) M.tPresencaMs = t; if (!p) M.tFimPresencaMs = t; presAnt = p; }
  if (emo.atual != emoAnt) { M.trocasEmocao++; emoAnt = emo.atual; if (emo.atual == Emocao::AMOR && M.tAmorMs < 0) M.tAmorMs = t; }
  if (emo.tUltimoSusto != tSustoAnt) { M.sustos++; tSustoAnt = emo.tUltimoSusto; if (M.tSustoMs < 0) M.tSustoMs = t; }
  if (emo.tUltimaInteracao != tInterAnt) { M.interacoes++; tInterAnt = emo.tUltimaInteracao; }
#ifdef TEM_PROX
  if (olhos.prox > 0.15f && M.tReageMs < 0) M.tReageMs = t;
  if (M.proxAnt >= 0) M.dProxMax = max(M.dProxMax, fabsf(olhos.prox - M.proxAnt));
  M.proxAnt = olhos.prox;
  if (t > 15000) { M.proxMin = min(M.proxMin, olhos.prox); M.proxMax = max(M.proxMax, olhos.prox); }
#else
  if (p && M.tReageMs < 0) M.tReageMs = t;
#endif
  bool v = olhos.convergencia > 0.35f;
  if (v != vesgoAnt) { M.trocasVesgo++; vesgoAnt = v; }
}
static void avancar(unsigned long ms) {
  for (unsigned long i = 0; i < ms; i++) {
    simMillis++;
    if (verdade && us.aguardarEco && !ecoPronto && millis() - us.tDisparo >= 2) { ecoDuracaoUs = leituraSimulada(); ecoPronto = true; }
    loop();
    if (verdade) medir();
  }
}
// ---- cenários (distância real em função do tempo, s)
static float nada(float) { return 999; }
static float parado45(float) { return 45; }
static float parede50(float) { return 50; }
static float parado20(float) { return 20; }
static float rapido(float t) { return t < 2 ? 999 : (t < 2.5f ? 120 - (t - 2) / 0.5f * 105 : 15); }      // 120 -> 15 cm em 0,5 s
static float lento(float t) { return t < 2 ? 999 : (t < 7 ? 120 - (t - 2) / 5 * 80 : 40); }              // 120 -> 40 cm em 5 s
static float sai(float t) { return t < 10 ? 45 + 3 * sinf(t) : 999; }
static float vesgo(float t) { return 10 + 1.5f * sinf(t * 0.8f); }
static float tecido(float t) { return 45 + 2 * sinf(t * 0.5f); }

int main(int argc, char** argv) {
  int c = argc > 1 ? atoi(argv[1]) : 1;
  if (argc > 2) rng.seed(atoi(argv[2]));
  WiFi.estado = WL_CONNECTED; setup();
  verdade = nada; avancar(9000);                                       // arranque, sem ninguém
  emo.energia = 70; emo.manualAtiva = false; emo.transitoriaAtiva = false;
  presAnt = pessoaPresente(millis()); emoAnt = emo.atual; tSustoAnt = emo.tUltimoSusto; tInterAnt = emo.tUltimaInteracao; vesgoAnt = false;
  float dur = 30; const char* nome = "";
  switch (c) {
    case 1: verdade = parado45; dur = 30; nome = "Pessoa parada a 45 cm (30 s)"; break;
    case 2: verdade = parede50; sigmaCm = 0.4f; pFalha = 0.02f; dur = 70; nome = "Parede a 50 cm (70 s)"; break;
    case 3: verdade = parado20; dur = 20; nome = "Pessoa a 20 cm (20 s)"; break;
    case 4: verdade = rapido; dur = 6; nome = "Aproximacao rapida 120->15 cm em 0,5 s"; break;
    case 5: verdade = lento; dur = 12; nome = "Aproximacao lenta 120->40 cm em 5 s"; break;
    case 6: verdade = sai; dur = 15; nome = "Pessoa sai aos 10 s"; break;
    case 7: verdade = vesgo; dur = 20; nome = "Muito perto, ~10 cm (20 s)"; break;
    case 8: verdade = tecido; sigmaCm = 4.0f; pFalha = 0.15f; dur = 30; nome = "Roupa macia a 45 cm: ruido 4 cm, 15% falhas"; break;
  }
  tCenario0 = millis();
  avancar((unsigned long)(dur * 1000));
  printf("%d|%s|%d|%d|%d|%d|%ld|%ld|%ld|%ld|%d|%s|reage %ld|prox %.3f-%.3f|dmax/ms %.4f\n", c, nome, M.trocasPresenca, M.sustos, M.trocasEmocao, M.trocasVesgo,
         M.tPresencaMs, M.tAmorMs, M.tSustoMs, M.tFimPresencaMs, M.interacoes, pessoaPresente(millis()) ? "sim" : "nao",
         M.tReageMs, M.proxMin, M.proxMax, M.dProxMax);
  return 0;
}
