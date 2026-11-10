#pragma once
#include <cstdint>
#include <map>
#include <string>
// Stub de Preferences (NVS) com chaves reais + atalhos usados pelos testes
struct Preferences {
  std::map<std::string, uint32_t> kv;
  uint8_t brilho = 0, paleta = 0; uint32_t cor = 0; bool tem = false, temPaleta = false, temCor = false;
  bool begin(const char*, bool) { return true; }
  bool existe(const char* k) const { return kv.count(k) > 0; }
  uint8_t getUChar(const char* k, uint8_t def) { auto it = kv.find(k); return it == kv.end() ? def : (uint8_t)it->second; }
  uint32_t getUInt(const char* k, uint32_t def) { auto it = kv.find(k); return it == kv.end() ? def : it->second; }
  size_t putUChar(const char* k, uint8_t v) { kv[k] = v; std::string s(k);
    if (s == "brilho") { brilho = v; tem = true; } if (s == "paleta") { paleta = v; temPaleta = true; } return 1; }
  size_t putUInt(const char* k, uint32_t v) { kv[k] = v; if (std::string(k) == "cor") { cor = v; temCor = true; } return 4; }
};
