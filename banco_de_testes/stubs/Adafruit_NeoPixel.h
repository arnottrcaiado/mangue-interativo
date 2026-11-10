// Stub que reproduz FIELMENTE a aritmética de brilho da Adafruit_NeoPixel
// (setBrightness guarda b+1; setPixelColor escala (c*brightness)>>8).
#pragma once
#include "Arduino.h"
// Codificação igual à Adafruit: bits 5-4 = posição do R no fio, 3-2 = G, 1-0 = B
typedef uint16_t neoPixelType;
#define NEO_RGB ((0 << 6) | (0 << 4) | (1 << 2) | (2))
#define NEO_RBG ((0 << 6) | (0 << 4) | (2 << 2) | (1))
#define NEO_GRB ((1 << 6) | (1 << 4) | (0 << 2) | (2))
#define NEO_GBR ((2 << 6) | (2 << 4) | (0 << 2) | (1))
#define NEO_BRG ((1 << 6) | (1 << 4) | (2 << 2) | (0))
#define NEO_BGR ((2 << 6) | (2 << 4) | (1 << 2) | (0))
#define NEO_KHZ800 0x0000
class Adafruit_NeoPixel {
 public:
  uint16_t n; uint8_t brightness = 0; uint8_t px[256][3] = {{0}};
  neoPixelType tipo;
  uint8_t fio[256][3] = {{0}};     // bytes como saem no fio (ordem do 'tipo')
  // O que um LED FÍSICO desta placa mostra: lê o fio como R,G,B (matriz RGB)
  void corFisica(int i, uint8_t* rgb) const { rgb[0] = fio[i][0]; rgb[1] = fio[i][1]; rgb[2] = fio[i][2]; }
  void updateType(neoPixelType t) { tipo = t; }
  void (*aoMostrar)(const Adafruit_NeoPixel&) = nullptr;
  Adafruit_NeoPixel(uint16_t num, int, neoPixelType t) : n(num), tipo(t) {}
  void begin() {}
  void clear() { memset(px, 0, sizeof(px)); }
  void show() { if (aoMostrar) aoMostrar(*this); }
  void setBrightness(uint8_t b) { brightness = b + 1; }
  static uint32_t Color(uint8_t r, uint8_t g, uint8_t b) { return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b; }
  void setPixelColor(uint16_t i, uint8_t r, uint8_t g, uint8_t b) {
    if (brightness) { r = (r * brightness) >> 8; g = (g * brightness) >> 8; b = (b * brightness) >> 8; }
    px[i][0] = r; px[i][1] = g; px[i][2] = b;
    fio[i][(tipo >> 4) & 3] = r; fio[i][(tipo >> 2) & 3] = g; fio[i][tipo & 3] = b;
  }
  void setPixelColor(uint16_t i, uint32_t c) { setPixelColor(i, (c >> 16) & 255, (c >> 8) & 255, c & 255); }
};
