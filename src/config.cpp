#include "config.h"
#include "secrets.h"
#include <ArduinoJson.h>
#include <SD.h>

AppConfig g_config;

static void setDefaults() {
    strlcpy(g_config.ap_ssid,        "SmartSpeaker",                    sizeof(g_config.ap_ssid));
    strlcpy(g_config.ap_pass,        "12345678",                        sizeof(g_config.ap_pass));
    strlcpy(g_config.sta_ssid, STA_SSID, sizeof(g_config.sta_ssid));
    strlcpy(g_config.sta_pass, STA_PASS, sizeof(g_config.sta_pass));
    g_config.wit_key[0]   = '\0';
    g_config.groq_key[0]  = '\0';
    strlcpy(g_config.ai_name,        "Jarvis",                                 sizeof(g_config.ai_name));
    strlcpy(g_config.city,           "YOUR_CITY",                              sizeof(g_config.city));
    strlcpy(g_config.master_prompt,
        "You are a helpful voice assistant. Keep answers short and clear — "
        "they will be spoken aloud, so avoid bullet points and markdown.",      sizeof(g_config.master_prompt));
    g_config.volume   = 30;
    g_config.mic_gain = 100;
}

void configLoad() {
    setDefaults();
    if (!SD.exists(CONFIG_FILE)) return;

    File f = SD.open(CONFIG_FILE, FILE_READ);
    if (!f) return;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) { Serial.println("config.json parse error, using defaults"); return; }

    strlcpy(g_config.ap_ssid,       doc["ap_ssid"]       | g_config.ap_ssid,        sizeof(g_config.ap_ssid));
    strlcpy(g_config.ap_pass,       doc["ap_pass"]       | g_config.ap_pass,         sizeof(g_config.ap_pass));
    { const char* v = doc["sta_ssid"] | ""; if (v[0]) strlcpy(g_config.sta_ssid, v, sizeof(g_config.sta_ssid)); }
    { const char* v = doc["sta_pass"] | ""; if (v[0]) strlcpy(g_config.sta_pass, v, sizeof(g_config.sta_pass)); }
    strlcpy(g_config.wit_key,       doc["wit_key"]       | "",                        sizeof(g_config.wit_key));
    strlcpy(g_config.groq_key,      doc["groq_key"]      | "",                        sizeof(g_config.groq_key));
    strlcpy(g_config.ai_name,       doc["ai_name"]       | g_config.ai_name,         sizeof(g_config.ai_name));
    strlcpy(g_config.city,          doc["city"]          | g_config.city,             sizeof(g_config.city));
    strlcpy(g_config.master_prompt, doc["master_prompt"] | g_config.master_prompt,    sizeof(g_config.master_prompt));
    g_config.volume   = doc["volume"]   | g_config.volume;
    g_config.mic_gain = doc["mic_gain"] | g_config.mic_gain;
}

void configSave() {
    JsonDocument doc;
    doc["ap_ssid"]       = g_config.ap_ssid;
    doc["ap_pass"]       = g_config.ap_pass;
    doc["sta_ssid"]      = g_config.sta_ssid;
    doc["sta_pass"]      = g_config.sta_pass;
    doc["wit_key"]       = g_config.wit_key;
    doc["groq_key"]      = g_config.groq_key;
    doc["ai_name"]       = g_config.ai_name;
    doc["city"]          = g_config.city;
    doc["master_prompt"] = g_config.master_prompt;
    doc["volume"]        = g_config.volume;
    doc["mic_gain"]      = g_config.mic_gain;

    SD.remove(CONFIG_FILE);
    File f = SD.open(CONFIG_FILE, FILE_WRITE);
    if (f) { serializeJson(doc, f); f.close(); }
}
