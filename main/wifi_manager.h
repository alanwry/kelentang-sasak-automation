#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <Preferences.h>

class WiFiManager {
public:
  WiFiManager();
  void begin();
  void update(); 

  // New streamlined modes
  void startAPMinimal(); // Setup mode
  void startSTAOnly();   // Normal operational mode

  void stopAll();

  // Settings
  void saveSettings(String ssid, String password, bool enableSTA);
  void getSettings(String &ssid, String &password, bool &enableSTA);
  bool isSTAEnabled();

private:
  Preferences prefs;
  String ssid;
  String password;
  bool enableSTA;
  bool isConnecting;
  unsigned long connectionStart;

  void loadFromPrefs();
};

extern WiFiManager wifiManager;

#endif
