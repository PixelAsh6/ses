#pragma once
#include <Arduino.h>

// ── SD Card SPI ───────────────────────────────────────────────────────────────
#define PIN_SD_MISO     13
#define PIN_SD_CLK      12
#define PIN_SD_MOSI     11
#define PIN_SD_CS       10

// ── I2S Output → GY-PCM5102 DAC ──────────────────────────────────────────────
#define PIN_I2S_BCK     4   // Bit clock
#define PIN_I2S_LRCK    5   // Left/right clock (word select)
#define PIN_I2S_DOUT    6   // Data out to DAC

// ── I2S Input ← INMP441 mic ───────────────────────────────────────────────────
#define PIN_MIC_WS      1   // Word select
#define PIN_MIC_SCK     2   // Bit clock
#define PIN_MIC_SD      21  // Data from mic

// ── Recording settings ────────────────────────────────────────────────────────
#define SAMPLE_RATE     16000   // 16 kHz — minimum accepted by Wit.ai
#define RECORD_SECS     7       // Duration of voice recording
#define I2S_PORT_IN     I2S_NUM_1  // Mic uses I2S1; audioI2S lib uses I2S0

// ── SD folder structure ───────────────────────────────────────────────────────
#define PATH_REC_IN     "/Recording_Input"
#define PATH_REC_OUT    "/Recording_Output"
#define PATH_MUSIC      "/Music"
#define PATH_PING       "/ping.mp3"
#define CONFIG_FILE     "/config.json"

// ── Runtime config (loaded from SD, editable in web UI) ──────────────────────
struct AppConfig {
    char ap_ssid[32];       // ESP32 hotspot SSID
    char ap_pass[32];       // ESP32 hotspot password (min 8 chars)
    char sta_ssid[64];      // Home/internet WiFi SSID
    char sta_pass[64];      // Home/internet WiFi password
    char wit_key[80];       // Wit.ai server-access token (STT + TTS)
    char groq_key[80];      // Groq API key (free AI model)
    char ai_name[32];       // Wake word / assistant name (e.g. "Jarvis")
    char city[64];          // City for weather lookups
    char master_prompt[512];// System prompt sent to AI before every message
    int  volume;            // 1–100 output volume (mapped to 0–21 for the audio lib)
    int  mic_gain;          // 1–200 mic sensitivity (100 = unity, stored in PSRAM buffer math)
};

extern AppConfig g_config;

void configLoad();
void configSave();
