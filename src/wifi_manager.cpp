#include "wifi_manager.h"
#include "config.h"
#include <WiFi.h>
#include <time.h>
#include <esp_wifi.h>

void wifiInit() {
    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleep(false);  // modem-sleep cycles radio → injects I2S noise
    esp_wifi_set_ps(WIFI_PS_NONE);  // disable power-save at driver level too, not just Arduino's

    // Always start the access point so the web UI is reachable even without internet
    WiFi.softAP(g_config.ap_ssid, g_config.ap_pass);
    Serial.print("AP started: ");
    Serial.print(g_config.ap_ssid);
    Serial.print("  IP: ");
    Serial.println(WiFi.softAPIP());

    // Try to join the home WiFi if credentials exist
    if (strlen(g_config.sta_ssid) > 0) {
        WiFi.begin(g_config.sta_ssid, g_config.sta_pass);
        Serial.print("Connecting to STA: ");
        Serial.print(g_config.sta_ssid);
        int tries = 30;
        while (WiFi.status() != WL_CONNECTED && tries-- > 0) {
            delay(1000);
            Serial.print('.');
        }
        if (WiFi.status() == WL_CONNECTED) {
            Serial.print("\nSTA IP: ");
            Serial.println(WiFi.localIP());
            configTzTime("EET-2EEST,M3.5.0/3,M10.5.0/4", "pool.ntp.org", "time.google.com");
            Serial.println("NTP sync started (EET/EEST)");
        } else {
            Serial.println("\nSTA failed (AP-only mode)");
        }
    }
}

bool wifiSTAConnected() { return WiFi.status() == WL_CONNECTED; }
String wifiAPIP()        { return WiFi.softAPIP().toString(); }
String wifiSTAIP()       { return WiFi.localIP().toString(); }
