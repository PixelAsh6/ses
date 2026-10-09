#pragma once
#include <Arduino.h>

// Call once at startup to configure I2S1 for the INMP441 microphone.
bool audioInInit();

// Record RECORD_SECS seconds from mic and save as 16kHz/16-bit/mono WAV.
// outPath must be a full SD path, e.g. "/Recording_Input/rec_001.wav"
// Returns true on success.
bool audioInRecord(const char* outPath);
