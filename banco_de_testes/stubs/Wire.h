// Stub I2C que emula um QMI8658 (registos relevantes) para testes no PC.
#pragma once
#include "Arduino.h"
struct SimQMI {
  bool presente = true;
  uint8_t endereco = 0x6B;
  uint8_t regs[256] = {0};
  float ax = 0, ay = 0, az = 1;   // gravidade no referencial da MATRIZ (x direita, y baixo), em g
  // Montagem física do chip face à matriz (como na placa real): sensor = M · R(-q·90°) · g
  int  montagemQuarto = 1;        // placa real: chip rodado 90° (relato do teste + exemplos públicos)
  bool montagemEspelho = false;
  SimQMI() { regs[0x00] = 0x05; }
  void atualizarDados() {
    float x = ax, y = ay, t;
    switch (((montagemQuarto % 4) + 4) % 4) {     // R(-q·90°)
      case 1: t = x; x = y;  y = -t; break;       // R(-90°): (y, -x)
      case 2: x = -x; y = -y; break;
      case 3: t = x; x = -y; y = t;  break;       // R(-270°) = R(90°): (-y, x)
      default: break;
    }
    if (montagemEspelho) x = -x;
    auto put = [&](int r, float g) { int16_t v = (int16_t)lroundf(g * 8192); regs[r] = v & 0xFF; regs[r + 1] = (v >> 8) & 0xFF; };
    put(0x35, x); put(0x37, y); put(0x39, az);
  }
};
extern SimQMI simQmi;
class TwoWire {
  uint8_t addr = 0, reg = 0; int nEscritos = 0; uint8_t buf[8]; int bi = 0, bn = 0;
 public:
  void begin(int, int, unsigned long) {}
  void setTimeOut(int) {}
  void beginTransmission(uint8_t a) { addr = a; nEscritos = 0; }
  void write(uint8_t v) {
    if (nEscritos == 0) reg = v; else if (simQmi.presente && addr == simQmi.endereco) simQmi.regs[reg] = v;
    nEscritos++;
  }
  uint8_t endTransmission(bool = true) { return (simQmi.presente && addr == simQmi.endereco) ? 0 : 2; }
  uint8_t requestFrom(uint8_t a, uint8_t n) {
    if (!(simQmi.presente && a == simQmi.endereco)) return 0;
    simQmi.atualizarDados();
    for (int i = 0; i < n; i++) buf[i] = simQmi.regs[reg + i];
    bi = 0; bn = n; return n;
  }
  uint8_t read() { return bi < bn ? buf[bi++] : 0; }
};
extern TwoWire Wire;
