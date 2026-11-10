#pragma once
#include "WiFi.h"
struct WiFiManager {
  bool portal = false;
  void setConfigPortalBlocking(bool) {}
  bool autoConnect(const char*) { return WiFi.estado == WL_CONNECTED; }
  void process() {}
  bool getConfigPortalActive() { return portal; }
  int timeout = 0; int pedidosPortal = 0;
  void setConfigPortalTimeout(int t) { timeout = t; }
  bool startConfigPortal(const char*) { portal = true; pedidosPortal++; return false; }
};
