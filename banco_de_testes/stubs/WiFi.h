#pragma once
#include "Arduino.h"
#define WL_CONNECTED 3
#define WIFI_STA 1
struct IPStub { String toString() const { return String("192.168.1.50"); } };
struct WiFiStub {
  int estado = 0;
  void mode(int) {}
  int status() { return estado; }
  int RSSI() { return -55; }
  IPStub localIP() { return IPStub(); }
};
extern WiFiStub WiFi;
