#include "wit_ai.h"
#include "config.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <SD.h>

// ── STT ───────────────────────────────────────────────────────────────────────
// Wit.ai returns multiple JSON objects (one per partial result, last is final).
// We read all of them and return the text from the last object that has
// "is_final": true.

String witSTT(const char* wavPath) {
    if (strlen(g_config.wit_key) == 0) { Serial.println("wit_key not set"); return ""; }

    File f = SD.open(wavPath, FILE_READ);
    if (!f) { Serial.println("witSTT: cannot open file"); return ""; }
    size_t fileSize = f.size();

    // Stream WAV from SD directly — no RAM buffer needed
    WiFiClientSecure sttClient;
    sttClient.setInsecure();
    HTTPClient http;
    http.begin(sttClient, "https://api.wit.ai/speech?v=20230215");
    http.addHeader("Authorization", String("Bearer ") + g_config.wit_key);
    http.addHeader("Content-Type",  "audio/wav");
    http.setTimeout(30000);

    int code = http.sendRequest("POST", (Stream*)&f, fileSize);
    f.close();

    if (code != 200) {
        Serial.printf("witSTT HTTP %d\n", code);
        http.end();
        return "";
    }

    String body = http.getString();
    http.end();

    // Parse concatenated JSON objects from the response stream
    String transcript = "";
    int idx = 0;
    while (idx < (int)body.length()) {
        int start = body.indexOf('{', idx);
        if (start < 0) break;

        // Find matching closing brace
        int depth = 0, end = -1;
        for (int i = start; i < (int)body.length(); i++) {
            if      (body[i] == '{') depth++;
            else if (body[i] == '}') { if (--depth == 0) { end = i; break; } }
        }
        if (end < 0) break;

        String chunk = body.substring(start, end + 1);
        JsonDocument doc;
        if (deserializeJson(doc, chunk) == DeserializationError::Ok) {
            bool isFinal = doc["is_final"] | false;
            const char* txt = doc["text"];
            if (isFinal && txt && strlen(txt) > 0) transcript = String(txt);
        }
        idx = end + 1;
    }

    Serial.println("STT result: " + transcript);
    return transcript;
}

// ── TTS ───────────────────────────────────────────────────────────────────────
// Wit.ai /synthesize returns audio/mpeg (MP3).  We stream it straight to SD.

bool witTTS(const String& text, const char* outPath) {
    if (strlen(g_config.wit_key) == 0) { Serial.println("wit_key not set"); return false; }

    JsonDocument req;
    req["q"]     = text;
    req["voice"] = "Rebecca";  // Wit.ai en-US voice
    req["speed"] = 100;
    req["pitch"] = 100;
    String body;
    serializeJson(req, body);

    WiFiClientSecure ttsClient;
    ttsClient.setInsecure();
    HTTPClient http;
    http.begin(ttsClient, "https://api.wit.ai/synthesize?v=20230215");
    http.addHeader("Authorization", String("Bearer ") + g_config.wit_key);
    http.addHeader("Content-Type",  "application/json");
    http.addHeader("Accept",        "audio/mpeg");
    http.setTimeout(20000);

    Serial.printf("witTTS: posting %d bytes, path=%s\n", body.length(), outPath);
    int code = http.POST(body);
    Serial.printf("witTTS: HTTP %d\n", code);
    if (code != 200) {
        Serial.printf("witTTS error: %s\n", http.getString().c_str());
        http.end();
        return false;
    }

    File f = SD.open(outPath, FILE_WRITE);
    if (!f) {
        Serial.println("witTTS: SD.open failed");
        http.end();
        return false;
    }

    int written = http.writeToStream(&f);
    f.close();
    http.end();
    Serial.printf("witTTS: writeToStream=%d bytes -> %s\n", written, outPath);
    return written > 0;
}
