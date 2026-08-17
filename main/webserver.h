#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <Arduino.h>

class WebServerManager {
public:
  void beginAPMinimal(); // AP Setup Dashboard
  void beginSTAFull();   // Normal Operation Dashboard
  void stop();
  void update();
  bool isActive() const;
};

extern WebServerManager webServer;

#endif
