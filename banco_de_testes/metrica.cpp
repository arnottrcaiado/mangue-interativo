// Mede "quanto pisca" uma versão do firmware (serve para V7.0 e V7.1).
// Compilar com -DINO="\"caminho/para/ficheiro.ino\""
#include INO
#include <vector>
unsigned long simMillis = 0; int simSoloMv = 1800;
SerialStub Serial; EspStub ESP; TwoWire Wire; SimQMI simQmi; WiFiStub WiFi; MDNSStub MDNS;

static std::vector<float> lum;            // luminância total por frame
static std::vector<std::vector<float>> cores;
static void capturar(const Adafruit_NeoPixel& m) {
  float L = 0; float r = 0, g = 0, b = 0;
  for (int i = 0; i < NUM_LEDS; i++) { L += 0.2126f * m.px[i][0] + 0.7152f * m.px[i][1] + 0.0722f * m.px[i][2]; r += m.px[i][0]; g += m.px[i][1]; b += m.px[i][2]; }
  lum.push_back(L); cores.push_back({r, g, b});
}
static void avancar(unsigned long ms) { for (unsigned long i = 0; i < ms; i++) { simMillis++; loop(); } }
static void emocao(const char* n) { server.argNome = String(n); server.argDur = String("600"); rotaEmocao(); }

int main() {
  srand(3);
  matriz.aoMostrar = capturar; WiFi.estado = WL_CONNECTED; setup(); avancar(4000);
  printf("%-9s %8s %10s %12s %10s %12s\n", "emocao", "brilho", "modulacao", "salto bruto", "quedas", "salto");
  printf("%-9s %8s %10s %12s %10s %12s\n", "", "medio", "percebida", "entre frames", "<50%", "percebido*");
  float somaMod = 0, piorSalto = 0; int totalQuedas = 0;
  for (uint8_t e = 0; e < (uint8_t)Emocao::TOTAL; e++) {
    emocao(EXPRESSOES[e].nome); avancar(3000); lum.clear();
    avancar(15000);
    // "percebido": integração visual ~40 ms (filtro exponencial por 20 ms reais, independente do FPS)
    float frameMs = 1000.0f * 15.0f / lum.size();
    float a = 1.0f - expf(-frameMs / 40.0f), pf = lum[0];
    std::vector<float> per; for (float v : lum) { pf += (v - pf) * a; per.push_back(pf); }
    float mn = 1e9, mx = 0, s = 0, salto = 0, saltoP = 0; int quedas = 0;
    for (size_t i = 50; i < per.size(); i++) { mn = std::min(mn, per[i]); mx = std::max(mx, per[i]); s += lum[i]; }
    float media = s / (lum.size() - 50);
    // saltos medidos em janelas de 20 ms (comparável entre 50 e 100 FPS)
    int passo = std::max(1, (int)lroundf(20.0f / frameMs));
    for (size_t i = 50 + passo; i < lum.size(); i += passo) {
      salto = std::max(salto, fabsf(lum[i] - lum[i - passo]) / media);
      saltoP = std::max(saltoP, fabsf(per[i] - per[i - passo]) / media);
      if (lum[i] < 0.5f * media && lum[i - passo] >= 0.5f * media) quedas++;
    }
    printf("%-9s %8.0f %9.0f%% %11.0f%% %10d %11.1f%%\n", EXPRESSOES[e].nome, media, 100 * (mx - mn) / media, 100 * salto, quedas, 100 * saltoP);
    somaMod += (mx - mn) / media; piorSalto = std::max(piorSalto, saltoP); totalQuedas += quedas;
  }
  printf("MEDIA modulacao percebida %.0f%% | pior salto percebido %.1f%% | quedas totais (15 s x 9) %d\n", 100 * somaMod / 9, 100 * piorSalto, totalQuedas);

  // Transição de cor: alegria (verde) -> susto (roxo): há "mergulho" de brilho?
  emocao("alegria"); avancar(4000); lum.clear(); avancar(2000);
  float ref = 0; for (float v : lum) ref += v; ref /= lum.size();
  lum.clear(); emocao("susto"); avancar(3000);
  float mn = 1e9; for (float v : lum) mn = std::min(mn, v);
  avancar(4000); lum.clear(); avancar(2000);
  float refB = 0; for (float v : lum) refB += v; refB /= lum.size(); ref = std::min(ref, refB);
  float durTrans = 0; for (size_t i = 1; i < cores.size(); i++) {}
  printf("TRANSICAO alegria->susto: brilho minimo durante a troca = %.0f%% do menor brilho estavel (origem/destino)\n", 100 * mn / ref);
  (void)durTrans;
  printf("* luminancia total integrada pelo olho (~40 ms), salto em janelas de 20 ms\n");
  return 0;
}
