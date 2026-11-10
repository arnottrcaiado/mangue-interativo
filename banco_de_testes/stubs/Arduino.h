// Stub mínimo do core Arduino/ESP32 para compilar e simular no PC.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <algorithm>
#include <strings.h>
using std::min; using std::max;
typedef uint8_t byte;
#define PROGMEM
#define PGM_P const char*
#define IRAM_ATTR
#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLDOWN 2
#define INPUT_PULLUP 4
#define CHANGE 3
#ifndef PI
#define PI 3.14159265358979f
#endif
extern unsigned long simMillis;
inline unsigned long millis() { return simMillis; }
inline unsigned long micros() { return simMillis * 1000UL; }
inline long random(long a, long b) { return a + rand() % (b - a); }
inline void delayMicroseconds(unsigned) {}
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
extern int simBotao;
inline int digitalRead(int p) { return p == 0 ? simBotao : 0; }
inline int digitalPinToInterrupt(int p) { return p; }
inline void attachInterrupt(int, void (*)(), int) {}
extern int simSoloMv;
inline uint32_t analogReadMilliVolts(int) { return simSoloMv; }
struct SerialStub {
  bool quiet = true;
  void begin(int) {}
  // Verificação de formato igual à do compilador do ESP32 (apanha %d/%s trocados)
  void printf(const char* f, ...) __attribute__((format(printf, 2, 3)));
  void println(const char* s) { if (!quiet) ::puts(s); }
};
extern SerialStub Serial;
#include <cstdarg>
inline void SerialStub::printf(const char* f, ...) { if (quiet) return; va_list a; va_start(a, f); ::vprintf(f, a); va_end(a); }
class String {
 public:
  std::string s;
  String(const char* c = "") : s(c) {}
  bool equalsIgnoreCase(const char* o) const { return strcasecmp(s.c_str(), o) == 0; }
  long toInt() const { return atol(s.c_str()); }
  const char* c_str() const { return s.c_str(); }
};
struct EspStub { uint32_t getFreeHeap() { return 200000; } };
extern EspStub ESP;
// ---- ESP-IDF: diagnóstico (stubs) ----
typedef enum { ESP_RST_UNKNOWN, ESP_RST_POWERON, ESP_RST_EXT, ESP_RST_SW, ESP_RST_PANIC, ESP_RST_INT_WDT,
               ESP_RST_TASK_WDT, ESP_RST_WDT, ESP_RST_DEEPSLEEP, ESP_RST_BROWNOUT, ESP_RST_SDIO } esp_reset_reason_t;
extern esp_reset_reason_t simMotivoReinicio;
inline esp_reset_reason_t esp_reset_reason() { return simMotivoReinicio; }
#define RTC_NOINIT_ATTR
extern unsigned simPilhaLivre;
inline unsigned uxTaskGetStackHighWaterMark(void*) { return simPilhaLivre; }
