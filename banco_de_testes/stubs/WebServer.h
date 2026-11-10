#pragma once
#include "Arduino.h"
#include <functional>
enum { HTTP_GET, HTTP_POST };
struct WebServer {
  String ultimaResposta; int ultimoCodigo = 0; String argNome, argDur;
  WebServer(int) {}
  void on(const char*, int, std::function<void()>) {}
  void onNotFound(std::function<void()>) {}
  bool ativo = false; int paragens = 0;
  void begin() { ativo = true; }
  void stop() { ativo = false; paragens++; }
  void handleClient() {}
  void send(int c, const char*, const char* body) { ultimoCodigo = c; ultimaResposta = String(body); }
  void send_P(int c, const char* t, const char* b) { send(c, t, b); }
  // "nome"/"valor" -> argNome ; "duracao"/"cor" -> argDur
  String arg(const char* n) { return (strcmp(n, "nome") == 0 || strcmp(n, "valor") == 0 || strcmp(n, "auto") == 0 || strcmp(n, "passo") == 0 || strcmp(n, "sentido") == 0) ? argNome : argDur; }
  bool hasArg(const char* n) { return (strcmp(n, "duracao") == 0 || strcmp(n, "cor") == 0) && !argDur.s.empty(); }
};
