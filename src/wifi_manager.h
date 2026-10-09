#pragma once
#include <Arduino.h>

// Start AP (always) and connect to STA if credentials are in g_config.
void wifiInit();

bool   wifiSTAConnected();
String wifiAPIP();
String wifiSTAIP();
