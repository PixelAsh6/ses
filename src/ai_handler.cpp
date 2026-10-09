#include "ai_handler.h"
#include "config.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

// Fetch weather from wttr.in (no API key needed) and return a plain-English summary.
static String getWeather() {
    if (strlen(g_config.city) == 0) return "";

    String city = String(g_config.city);
    city.replace(" ", "+");

    HTTPClient http;
    http.begin("http://wttr.in/" + city + "?format=j1");
    http.setTimeout(10000);
    http.addHeader("User-Agent", "SmartSpeaker/1.0");
    int code = http.GET();
    if (code != 200) { http.end(); return ""; }

    String resp = http.getString();
    http.end();

    JsonDocument doc;
    if (deserializeJson(doc, resp) != DeserializationError::Ok) return "";

    const char* desc  = doc["current_condition"][0]["weatherDesc"][0]["value"] | "unknown";
    const char* tempC = doc["current_condition"][0]["temp_C"]         | "?";
    const char* humid = doc["current_condition"][0]["humidity"]        | "?";
    const char* wind  = doc["current_condition"][0]["windspeedKmph"]   | "?";

    char buf[256];
    snprintf(buf, sizeof(buf),
        "Current weather in %s: %s, %s C, humidity %s%%, wind %s km/h.",
        g_config.city, desc, tempC, humid, wind);
    return String(buf);
}

String aiQuery(const String& query) {
    if (strlen(g_config.groq_key) == 0) return "Groq API key not configured.";

    // Build system prompt from the master prompt stored in config
    String sysPrompt = String(g_config.master_prompt);

    // Detect weather intent and inject live data
    String lq = query; lq.toLowerCase();
    bool wantWeather = lq.indexOf("weather") >= 0  ||
                       lq.indexOf("temperature") >= 0 ||
                       lq.indexOf("forecast") >= 0 ||
                       lq.indexOf("hot") >= 0 ||
                       lq.indexOf("cold") >= 0 ||
                       lq.indexOf("rain") >= 0;

    if (wantWeather) {
        String wx = getWeather();
        if (wx.length() > 0) sysPrompt += "\n\nReal-time weather data:\n" + wx;
    }

    // Build Groq API request (OpenAI-compatible format)
    JsonDocument req;
    req["model"]      = "llama-3.1-8b-instant";  // Free Groq model
    req["max_tokens"] = 200;               // Keeps TTS audio short

    JsonArray msgs = req["messages"].to<JsonArray>();
    JsonObject sys = msgs.add<JsonObject>();
    sys["role"]    = "system";
    sys["content"] = sysPrompt;

    JsonObject usr = msgs.add<JsonObject>();
    usr["role"]    = "user";
    usr["content"] = query;

    String body;
    serializeJson(req, body);

    WiFiClientSecure groqClient;
    groqClient.setInsecure();
    HTTPClient http;
    http.begin(groqClient, "https://api.groq.com/openai/v1/chat/completions");
    http.addHeader("Authorization", String("Bearer ") + g_config.groq_key);
    http.addHeader("Content-Type",  "application/json");
    http.setTimeout(15000);

    int code = http.POST(body);
    if (code != 200) {
        String err = http.getString();
        http.end();
        Serial.printf("Groq HTTP %d: %s\n", code, err.c_str());
        return "AI error, check serial log.";
    }

    String resp = http.getString();
    http.end();

    JsonDocument doc;
    if (deserializeJson(doc, resp) != DeserializationError::Ok) return "AI parse error.";

    const char* content = doc["choices"][0]["message"]["content"];
    String answer = content ? String(content) : "No answer.";
    Serial.println("AI: " + answer);
    return answer;
}
