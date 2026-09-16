#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <Arduino.h>
#include <esp_http_server.h>

class WebServerManager {
public:
  void beginAPMinimal();
  void beginSTAFull();
  void stop();
  void update();
  bool isActive() const;
};

void sendLogToClients(const char* message);
void LOG(const char* format, ...);

extern WebServerManager webServer;

#endif