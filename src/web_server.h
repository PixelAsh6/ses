#pragma once
#include <Arduino.h>

void webServerInit();

// Call from main loop — not required for AsyncWebServer but kept
// for any future polling work.
void webServerLoop();

// Thread-safe status string read/write (used by the record task).
void   statusSet(const String& s);
String statusGet();
