#include <Arduino.h>
#include <LittleFS.h>
#include "config.h"
#include "sd_manager.h"
#include "audio_in.h"
#include "audio_out.h"
#include "music.h"
#include "wifi_manager.h"
#include "web_server.h"

void setup() {
    Serial.begin(115200);
    delay(3000); // Give USB CDC time to enumerate so boot log is visible
    Serial.println("\n=== Smart Speaker booting ===");

    // ── 1. SD card — everything else depends on it ───────────────────────────
    if (!sdInit()) {
        Serial.println("FATAL: SD card not found. Check wiring.");
        while (true) delay(1000);
    }

    // ── 2. Config — load from SD (or use defaults) ───────────────────────────
    configLoad();
    Serial.printf("AI name: %s | Volume: %d\n", g_config.ai_name, g_config.volume);

    // ── 3. WiFi — AP always up; STA if credentials exist ────────────────────
    wifiInit();

    // ── 4. Audio output — must come after WiFi (no conflict, just ordering) ──
    if (!audioOutInit()) {
        Serial.println("Audio output init failed");
    }
    audioOutSetVolume(g_config.volume);

    // ── 5. Microphone ────────────────────────────────────────────────────────
    if (!audioInInit()) {
        Serial.println("Mic (INMP441) init failed — check wiring");
        // Non-fatal: web UI still works, recording will return error
    }

    // ── 6. Music playlist ────────────────────────────────────────────────────
    musicScan();

    // ── 7. LittleFS (HTML files) ─────────────────────────────────────────────
    // partitionLabel must match the label in partitions.csv ("littlefs")
    if (!LittleFS.begin(false, "/littlefs", 10, "littlefs")) {
        Serial.println("LittleFS not mounted — run 'pio run -t uploadfs' once");
    }

    // ── 8. Web server ────────────────────────────────────────────────────────
    webServerInit();

    Serial.println("Boot complete.");
    Serial.printf("Open http://%s in your browser\n", wifiAPIP().c_str());
}

void loop() {
    // ESP32-audioI2S requires this to be called every iteration to feed
    // the I2S DMA buffer with decoded audio frames.
    audioOutLoop();

    // Minimal yield — prevents watchdog resets when audio is idle
    delay(1);
}
