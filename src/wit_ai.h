#pragma once
#include <Arduino.h>

// Send a WAV file from SD to Wit.ai Speech-to-Text.
// Returns the final transcript string, or "" on error.
String witSTT(const char* wavPath);

// Send text to Wit.ai Text-to-Speech and save the resulting MP3 to outPath on SD.
// Returns true on success.
bool witTTS(const String& text, const char* outPath);
