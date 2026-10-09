#include "web_server.h"
#include "config.h"
#include <LittleFS.h>
#include "audio_in.h"
#include "audio_out.h"
#include "music.h"
#include "wit_ai.h"
#include "ai_handler.h"
#include "wifi_manager.h"

#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <SD.h>
#include <WiFi.h>
#include <time.h>

static AsyncWebServer server(80);

// ── Shared state between main task and record task ───────────────────────────
static volatile bool   g_busy  = false;
static String          g_statusMsg    = "Ready";
static String          g_lastHeard    = "";
static String          g_lastAnswer   = "";
static portMUX_TYPE    g_mux   = portMUX_INITIALIZER_UNLOCKED;

void statusSet(const String& s) {
    portENTER_CRITICAL(&g_mux);
    g_statusMsg = s;
    portEXIT_CRITICAL(&g_mux);
    Serial.println("[status] " + s);
}
String statusGet() {
    portENTER_CRITICAL(&g_mux);
    String s = g_statusMsg;
    portEXIT_CRITICAL(&g_mux);
    return s;
}

// ── Upload helper ─────────────────────────────────────────────────────────────
static File g_uploadFile;

// ── Timestamp / sequential filename ──────────────────────────────────────────
static int g_recCounter = 0;
static String getRecTimestamp() {
    struct tm ti;
    if (getLocalTime(&ti, 100) && ti.tm_year > 100) {
        char buf[20];
        snprintf(buf, sizeof(buf), "%02d.%02d-%02d.%02d",
                 ti.tm_mon + 1, ti.tm_mday, ti.tm_hour, ti.tm_min);
        return String(buf);
    }
    return "";
}

// Returns full path of lexicographically newest .mp3 in dirPath, or "".
static String findNewestMp3(const char* dirPath) {
    File dir = SD.open(dirPath);
    if (!dir || !dir.isDirectory()) return "";
    String newest = "";
    while (true) {
        File f = dir.openNextFile();
        if (!f) break;
        if (!f.isDirectory()) {
            String name = f.name();
            int sl = name.lastIndexOf('/');
            if (sl >= 0) name = name.substring(sl + 1);
            if (name.endsWith(".mp3") && name > newest) newest = name;
        }
        f.close();
    }
    dir.close();
    if (newest.isEmpty()) return "";
    return String(dirPath) + "/" + newest;
}

// ── HTML pages ────────────────────────────────────────────────────────────────

static const char HTML_MAIN[] PROGMEM = R"HTML(<!DOCTYPE html>
<html>
<head><title>Smart Speaker</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
</head>
<body>
<h1>Smart Speaker</h1>

<p><b>Status:</b> <span id="st">Loading...</span></p>

<button id="recbtn" onclick="doRecord()">Record 7 s</button>
<span id="countdown"></span>

<hr>
<h2>Music</h2>
<p><b>Now playing:</b> <span id="np">-</span></p>
<button onclick="fetch('/prev').then(()=>refresh())">Previous</button>
<button onclick="fetch('/next').then(()=>refresh())">Next</button>

<p>Output volume (1-100): <input type="range" min="1" max="100" id="vol" value="70"
   oninput="document.getElementById('vv').textContent=this.value"
   onchange="setVol(this.value)"> <span id="vv">70</span>%
</p>
<p>Mic sensitivity (1-200, 100=normal): <input type="range" min="1" max="200" id="mic" value="100"
   oninput="document.getElementById('mv').textContent=this.value"
   onchange="setMic(this.value)"> <span id="mv">100</span>
</p>

<hr>
<h2>Upload song to /Music</h2>
<form method="POST" action="/upload" enctype="multipart/form-data">
  <input type="file" name="file" accept=".mp3" required>
  <input type="submit" value="Upload MP3">
</form>

<hr>
<p><a href="/files">File Browser</a> | <a href="/settings">Settings</a></p>

<script>
function refresh(){
  fetch('/status').then(r=>r.json()).then(d=>{
    document.getElementById('st').textContent = d.status;
    document.getElementById('np').textContent = d.playing;
    document.getElementById('vol').value      = d.volume;
    document.getElementById('vv').textContent = d.volume;
    document.getElementById('mic').value      = d.mic_gain;
    document.getElementById('mv').textContent = d.mic_gain;
  });
}
function doRecord(){
  var btn=document.getElementById('recbtn');
  btn.disabled=true;
  var cd=document.getElementById('countdown');
  var n=7;
  cd.textContent=' '+n+'s';
  var countdown=setInterval(()=>{n--;cd.textContent=n>0?' '+n+'s':'';if(n<=0)clearInterval(countdown);},1000);
  fetch('/record',{method:'POST'}).then(r=>r.json()).then(()=>{
    // Poll /status until not busy
    var poll=setInterval(()=>{
      fetch('/status').then(r=>r.json()).then(d=>{
        document.getElementById('st').textContent=d.status;
        document.getElementById('np').textContent=d.playing;
        if(!d.busy){clearInterval(poll);btn.disabled=false;cd.textContent='';}
      });
    },1000);
  }).catch(()=>{clearInterval(countdown);cd.textContent='';btn.disabled=false;});
}
function setVol(v){
  fetch('/volume',{method:'POST',
    headers:{'Content-Type':'application/json'},
    body:JSON.stringify({volume:parseInt(v)})});
}
function setMic(v){
  fetch('/mic-gain',{method:'POST',
    headers:{'Content-Type':'application/json'},
    body:JSON.stringify({mic_gain:parseInt(v)})});
}
refresh();
setInterval(refresh,4000);
</script>
</body>
</html>)HTML";

// ── Settings page ─────────────────────────────────────────────────────────────
static String buildSettings() {
    String html = "<!DOCTYPE html><html><head><title>Settings</title></head><body>";
    html += "<h1>Settings</h1>";
    html += "<form method='POST' action='/settings'>";

    html += "<h2>ESP32 Hotspot (AP)</h2>";
    html += "SSID: <input name='ap_ssid' value='" + String(g_config.ap_ssid) + "'><br>";
    html += "Password (min 8 chars): <input name='ap_pass' value='" + String(g_config.ap_pass) + "'><br>";

    html += "<h2>Home WiFi (for internet)</h2>";
    html += "SSID: <input name='sta_ssid' value='" + String(g_config.sta_ssid) + "'><br>";
    html += "Password: <input name='sta_pass' value='" + String(g_config.sta_pass) + "'><br>";

    html += "<h2>API Keys</h2>";
    html += "Wit.ai key: <input name='wit_key' size='40' value='" + String(g_config.wit_key) + "'><br>";
    html += "Groq key: <input name='groq_key' size='40' value='" + String(g_config.groq_key) + "'><br>";

    html += "<h2>Assistant</h2>";
    html += "AI name (wake word): <input name='ai_name' value='" + String(g_config.ai_name) + "'><br>";
    html += "City (for weather): <input name='city' value='" + String(g_config.city) + "'><br>";
    html += "Output volume (1-100): <input name='volume' type='number' min='1' max='100' value='" + String(g_config.volume) + "'><br>";
    html += "Mic sensitivity (1-200, 100=normal): <input name='mic_gain' type='number' min='1' max='200' value='" + String(g_config.mic_gain) + "'><br>";
    html += "Master prompt:<br><textarea name='master_prompt' rows='5' cols='60'>" + String(g_config.master_prompt) + "</textarea><br>";

    html += "<br><input type='submit' value='Save'>";
    html += "</form>";
    html += "<p><a href='/'>Back</a></p>";
    html += "</body></html>";
    return html;
}

// ── File browser ──────────────────────────────────────────────────────────────
static String buildFileBrowser(const String& path) {
    String html = "<!DOCTYPE html><html><head><title>Files</title></head><body>";
    html += "<h1>Files: " + path + "</h1>";

    // Up link
    if (path != "/") {
        int sl = path.lastIndexOf('/');
        String parent = (sl > 0) ? path.substring(0, sl) : "/";
        html += "<p><a href='/files?path=" + parent + "'>[Up]</a></p>";
    }

    File dir = SD.open(path.c_str());
    if (!dir || !dir.isDirectory()) {
        html += "<p>Cannot open directory.</p></body></html>";
        return html;
    }

    html += "<ul>";
    while (true) {
        File f = dir.openNextFile();
        if (!f) break;
        String fname = f.name();
        int sl2 = fname.lastIndexOf('/');
        String shortName = (sl2 >= 0) ? fname.substring(sl2 + 1) : fname;
        bool isDir = f.isDirectory();
        f.close();

        if (isDir) {
            html += "<li>[DIR] <a href='/files?path=" + fname + "'>" + shortName + "</a>";
        } else {
            html += "<li>" + shortName;
        }

        // Delete form
        html += " <form style='display:inline' method='POST' action='/delete'"
                " onsubmit=\"return confirm('Delete " + shortName + "?')\">"
                "<input type='hidden' name='path' value='" + fname + "'>"
                "<input type='submit' value='Delete'></form>";
        html += "</li>";
    }
    dir.close();

    html += "</ul>";
    html += "<p><a href='/'>Back to main</a></p>";
    html += "</body></html>";
    return html;
}

// ── Record + command processing task ─────────────────────────────────────────
static void recordTask(void* /*param*/) {
    audioOutStop();  // eliberează MP3 decoder dacă muzica rulează
    for (int i = 0; i < 50; i++) {
        if (!audioOutIsPlaying()) break;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    vTaskDelay(pdMS_TO_TICKS(20));

    unsigned long cycleStartMs  = millis();
    uint32_t      heapCycleStart = ESP.getFreeHeap();
    unsigned long sttDurMs = 0, aiDurMs = 0, ttsDurMs = 0;

    char wavPath[64];
    String ts = getRecTimestamp();
    if (ts.length())
        snprintf(wavPath, sizeof(wavPath), PATH_REC_IN "/%s.wav", ts.c_str());
    else {
        g_recCounter++;
        snprintf(wavPath, sizeof(wavPath), PATH_REC_IN "/%d.wav", g_recCounter);
    }

    // --- Step 1: Record ---
    audioOutStop();

    unsigned long t1Start  = millis();
    uint32_t      h1Before = ESP.getFreeHeap();
    bool recOk = audioInRecord(wavPath);
    unsigned long t1Dur   = millis() - t1Start;
    uint32_t      h1After = ESP.getFreeHeap();

    Serial.println("--- Etapa 1: Înregistrare audio ---");
    Serial.printf("  Durata: %lu ms\n", t1Dur);
    Serial.printf("  Heap înainte: %u bytes | după: %u bytes | diferență: %d bytes\n",
                   h1Before, h1After, (int32_t)h1After - (int32_t)h1Before);
    Serial.println(recOk ? "  Rezultat: OK" : "  Rezultat: EROARE (recording failed)");

    if (!recOk) {
        statusSet("Error: recording failed");
        g_busy = false;
        vTaskDelete(NULL);
        return;
    }

    // --- Step 2: Speech-to-text ---
    statusSet("Processing speech...");

    unsigned long t2Start  = millis();
    uint32_t      h2Before = ESP.getFreeHeap();
    String transcript = witSTT(wavPath);
    unsigned long t2Dur   = millis() - t2Start;
    uint32_t      h2After = ESP.getFreeHeap();
    sttDurMs = t2Dur;

    Serial.println("--- Etapa 2: Transcriere vocală (Wit.ai STT) ---");
    Serial.printf("  Durata: %lu ms\n", t2Dur);
    Serial.printf("  Heap înainte: %u bytes | după: %u bytes | diferență: %d bytes\n",
                   h2Before, h2After, (int32_t)h2After - (int32_t)h2Before);
    Serial.println("  Text detectat: \"" + transcript + "\"");
    Serial.println(transcript.isEmpty() ? "  Rezultat: EROARE - nimic detectat" : "  Rezultat: OK");

    if (transcript.isEmpty()) {
        statusSet("Error: STT failed or nothing heard");
        g_busy = false;
        vTaskDelete(NULL);
        return;
    }

    g_lastHeard = transcript;

    // --- Step 3: Parse command ---
    String lowerT    = transcript;  lowerT.toLowerCase();    lowerT.trim();
    String lowerName = String(g_config.ai_name); lowerName.toLowerCase();

    // Find wake word anywhere in utterance (handles "Hey Jarvis ...")
    int nameIdx = lowerT.indexOf(lowerName);

    Serial.println("--- Etapa 3: Detecție cuvânt de activare ---");
    Serial.println("  Cuvânt căutat: \"" + lowerName + "\"");
    Serial.println("  Text primit: \"" + transcript + "\"");

    if (nameIdx < 0) {
        Serial.println("  Comandă extrasă: \"\"");
        Serial.println("  Rezultat: NEDETECTAT");

        g_lastAnswer = "(wake word not detected)";
        statusSet("Ready");
        g_busy = false;
        vTaskDelete(NULL);
        return;
    }

    String command = transcript.substring(nameIdx + lowerName.length());
    command.trim();
    String lowerCmd = command; lowerCmd.toLowerCase();

    Serial.println("  Comandă extrasă: \"" + command + "\"");
    Serial.println("  Rezultat: DETECTAT");

    // --- Step 4: Route to music or AI ---
    if (lowerCmd.startsWith("play ") || lowerCmd == "play") {
        // ── Music mode ──────────────────────────────────────────────────────
        audioOutPlayPing();

        String query = (lowerCmd.length() > 5) ? command.substring(5) : "";
        query.trim();

        int idx = musicFindBest(query);
        if (idx >= 0) {
            g_currentSong = idx;
            musicPlayCurrent();
            delay(100);  // timp pentru ca audioOutPlay să inițializeze bufferele
            g_lastAnswer = "Playing: " + g_playlist[idx].name;
        } else {
            g_lastAnswer = "Song not found: \"" + query + "\"";
        }

    } else {
        // ── AI mode ─────────────────────────────────────────────────────────
        statusSet("Thinking...");

        unsigned long t4Start  = millis();
        uint32_t      h4Before = ESP.getFreeHeap();
        String answer = aiQuery(command);
        unsigned long t4Dur   = millis() - t4Start;
        uint32_t      h4After = ESP.getFreeHeap();
        aiDurMs = t4Dur;

        long estimatedTokens = (long)(answer.length() / 4);

        Serial.println("--- Etapa 4: Interogare AI (Groq) ---");
        Serial.printf("  Durata: %lu ms\n", t4Dur);
        Serial.printf("  Heap înainte: %u bytes | după: %u bytes | diferență: %d bytes\n",
                       h4Before, h4After, (int32_t)h4After - (int32_t)h4Before);
        Serial.println("  Tokens estimați în răspuns: " + String(estimatedTokens) + " (număr cuvinte * 1.3)");
        Serial.println("  Răspuns: \"" + answer + "\"");
        Serial.println(answer.isEmpty() ? "  Rezultat: EROARE" : "  Rezultat: OK");

        statusSet("Generating speech...");
        char ttsPath[64];
        String tts_ts = getRecTimestamp();
        if (tts_ts.length())
            snprintf(ttsPath, sizeof(ttsPath), PATH_REC_OUT "/%s.mp3", tts_ts.c_str());
        else
            snprintf(ttsPath, sizeof(ttsPath), PATH_REC_OUT "/%d.mp3", g_recCounter);

        audioOutPlayPing();

        g_lastAnswer = answer;

        unsigned long t5Start  = millis();
        uint32_t      h5Before = ESP.getFreeHeap();
        bool ttsOk = witTTS(answer, ttsPath);
        unsigned long t5Dur   = millis() - t5Start;
        uint32_t      h5After = ESP.getFreeHeap();
        ttsDurMs = t5Dur;

        Serial.println("--- Etapa 5: Sinteză vocală (Wit.ai TTS) ---");
        Serial.printf("  Durata: %lu ms\n", t5Dur);
        Serial.printf("  Heap înainte: %u bytes | după: %u bytes | diferență: %d bytes\n",
                       h5Before, h5After, (int32_t)h5After - (int32_t)h5Before);
        Serial.println("  Fișier salvat: \"" + String(ttsPath) + "\"");
        Serial.println(ttsOk ? "  Rezultat: OK" : "  Rezultat: EROARE");

        if (ttsOk) {
            String playPath = findNewestMp3(PATH_REC_OUT);
            Serial.println("TTS play path: " + playPath);
            if (playPath.length() > 0) {
                unsigned long t6Start = millis();
                audioOutPlaySync(playPath.c_str());
                unsigned long t6Dur = millis() - t6Start;

                Serial.println("--- Etapa 6: Redare audio ---");
                Serial.printf("  Durata: %lu ms\n", t6Dur);
                Serial.println("  Fișier redat: \"" + playPath + "\"");
            }
        }
    }

    unsigned long cycleTotalMs = millis() - cycleStartMs;
    uint32_t      heapCycleEnd = ESP.getFreeHeap();

    Serial.println("========================================");
    Serial.println("SUMAR CICLU COMPLET");
    Serial.printf("  Timp total: %lu ms\n", cycleTotalMs);
    Serial.printf("  STT:        %lu ms\n", sttDurMs);
    Serial.printf("  AI:         %lu ms\n", aiDurMs);
    Serial.printf("  TTS:        %lu ms\n", ttsDurMs);
    Serial.printf("  Heap la start:   %u bytes\n", heapCycleStart);
    Serial.printf("  Heap la final:   %u bytes\n", heapCycleEnd);
    Serial.printf("  Diferență heap:  %d bytes (pozitiv = pierdere, negativ = eliberat)\n",
                   (int32_t)heapCycleStart - (int32_t)heapCycleEnd);
    Serial.println("========================================");

    statusSet("Ready");

    g_busy = false;
    vTaskDelete(NULL);
}

// ── Server initialisation ─────────────────────────────────────────────────────
void webServerInit() {

    // Main page — serve from LittleFS, fallback to inline
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        if (LittleFS.exists("/index.html"))
            req->send(LittleFS, "/index.html", "text/html");
        else
            req->send_P(200, "text/html", HTML_MAIN);
    });

    // Status JSON
    server.on("/status", HTTP_GET, [](AsyncWebServerRequest* req) {
        JsonDocument doc;
        String st = statusGet();
        doc["status"]         = st;
        doc["status_visible"] = (st != "Ready");
        String np = musicCurrentName();
        doc["playing"]        = (np == "None") ? "" : np;
        doc["playing_active"] = audioOutIsPlaying();
        doc["volume"]         = g_config.volume;
        doc["mic_gain"]       = g_config.mic_gain;
        doc["busy"]           = (bool)g_busy;
        doc["heard"]          = g_lastHeard;
        doc["answer"]         = g_lastAnswer;
        if (g_currentSong >= 0 && g_currentSong < (int)g_playlist.size())
            doc["duration_s"] = g_playlist[g_currentSong].duration_s;
        else
            doc["duration_s"] = 0;
        String out;
        serializeJson(doc, out);
        req->send(200, "application/json", out);
    });

    // Record — starts background task and returns immediately.
    // Browser polls /status to track progress.
    server.on("/record", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (g_busy) {
            req->send(200, "application/json", "{\"status\":\"busy\"}");
            return;
        }
        g_busy = true;
        statusSet("Recording...");
        xTaskCreate(recordTask, "rec", 12288, NULL, 2, NULL);
        req->send(200, "application/json", "{\"status\":\"Recording started — polling status\"}");
    });

    // Volume
    server.on("/volume", HTTP_POST, [](AsyncWebServerRequest* req) {}, NULL,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
            JsonDocument doc;
            if (deserializeJson(doc, data, len) == DeserializationError::Ok) {
                int v = doc["volume"] | g_config.volume;
                audioOutSetVolume(v);
                configSave();
            }
            req->send(200, "application/json", "{\"ok\":true}");
        });

    // Mic gain (1–200)
    server.on("/mic-gain", HTTP_POST, [](AsyncWebServerRequest* req) {}, NULL,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
            JsonDocument doc;
            if (deserializeJson(doc, data, len) == DeserializationError::Ok) {
                int v = doc["mic_gain"] | g_config.mic_gain;
                g_config.mic_gain = constrain(v, 1, 200);
                configSave();
            }
            req->send(200, "application/json", "{\"ok\":true}");
        });

    // Play a specific file by path (used by file browser click)
    server.on("/play-file", HTTP_GET, [](AsyncWebServerRequest* req) {
        if (!g_busy && req->hasParam("path")) {
            String p = req->getParam("path")->value();
            for (int i = 0; i < (int)g_playlist.size(); i++) {
                if (g_playlist[i].path == p) { g_currentSong = i; break; }
            }
            audioOutPlay(p.c_str());
        }
        req->send(200, "application/json", "{\"ok\":true}");
    });

    // Next / prev / play-pause
    server.on("/next", HTTP_GET, [](AsyncWebServerRequest* req) {
        if (!g_busy) musicNext();
        req->send(200, "text/plain", "ok");
    });
    server.on("/prev", HTTP_GET, [](AsyncWebServerRequest* req) {
        if (!g_busy) musicPrev();
        req->send(200, "text/plain", "ok");
    });
    server.on("/play-pause", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (!g_busy) {
            if (audioOutIsPlaying()) {
                audioOutStop();
            } else if (!g_playlist.empty()) {
                if (g_currentSong < 0) g_currentSong = 0;
                musicPlayCurrent();
            }
        }
        req->send(200, "application/json", "{\"ok\":true}");
    });

    // Settings GET — settings are now a tab in index.html; redirect to root
    server.on("/settings", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->redirect("/");
    });

    // Settings JSON — used by settings.html to pre-populate the form via JS
    server.on("/settings-json", HTTP_GET, [](AsyncWebServerRequest* req) {
        JsonDocument doc;
        doc["ap_ssid"]       = g_config.ap_ssid;
        doc["ap_pass"]       = g_config.ap_pass;
        doc["sta_ssid"]      = g_config.sta_ssid;
        doc["sta_pass"]      = g_config.sta_pass;
        doc["wit_key"]       = g_config.wit_key;
        doc["groq_key"]      = g_config.groq_key;
        doc["ai_name"]       = g_config.ai_name;
        doc["city"]          = g_config.city;
        doc["volume"]        = g_config.volume;
        doc["mic_gain"]      = g_config.mic_gain;
        doc["master_prompt"] = g_config.master_prompt;
        String out; serializeJson(doc, out);
        req->send(200, "application/json", out);
    });

    // Settings POST
    server.on("/settings", HTTP_POST, [](AsyncWebServerRequest* req) {
        auto param = [&](const char* name, char* dest, size_t sz) {
            if (req->hasParam(name, true))
                strlcpy(dest, req->getParam(name, true)->value().c_str(), sz);
        };
        param("ap_ssid",       g_config.ap_ssid,       sizeof(g_config.ap_ssid));
        param("ap_pass",       g_config.ap_pass,        sizeof(g_config.ap_pass));
        param("sta_ssid",      g_config.sta_ssid,       sizeof(g_config.sta_ssid));
        param("sta_pass",      g_config.sta_pass,       sizeof(g_config.sta_pass));
        param("wit_key",       g_config.wit_key,        sizeof(g_config.wit_key));
        param("groq_key",      g_config.groq_key,       sizeof(g_config.groq_key));
        param("ai_name",       g_config.ai_name,        sizeof(g_config.ai_name));
        param("city",          g_config.city,            sizeof(g_config.city));
        param("master_prompt", g_config.master_prompt,  sizeof(g_config.master_prompt));

        if (req->hasParam("volume", true))
            g_config.volume = req->getParam("volume", true)->value().toInt();
        if (req->hasParam("mic_gain", true))
            g_config.mic_gain = constrain(req->getParam("mic_gain", true)->value().toInt(), 1, 200);

        configSave();

        // Reconnect WiFi with new credentials
        WiFi.softAP(g_config.ap_ssid, g_config.ap_pass);
        if (strlen(g_config.sta_ssid) > 0) {
            WiFi.begin(g_config.sta_ssid, g_config.sta_pass);
        }
        audioOutSetVolume(g_config.volume);

        req->send(200, "application/json", "{\"ok\":true}");
    });

    // File browser (legacy HTML — kept for direct access)
    server.on("/files", HTTP_GET, [](AsyncWebServerRequest* req) {
        String path = req->hasParam("path") ? req->getParam("path")->value() : "/";
        req->send(200, "text/html", buildFileBrowser(path));
    });

    // File browser JSON — used by the embedded file browser in index.html
    server.on("/files-json", HTTP_GET, [](AsyncWebServerRequest* req) {
        String path = req->hasParam("path") ? req->getParam("path")->value() : "/";
        if (!path.startsWith("/")) path = "/" + path;
        JsonDocument doc;
        doc["path"] = path;
        JsonArray entries = doc["entries"].to<JsonArray>();
        File dir = SD.open(path.c_str());
        if (dir && dir.isDirectory()) {
            while (true) {
                File f = dir.openNextFile();
                if (!f) break;
                String fname = f.name();
                // Normalize to absolute path — f.name() may omit leading /
                if (!fname.startsWith("/")) {
                    fname = (path == "/" ? "/" : path + "/") + fname;
                }
                bool isDir = f.isDirectory();
                size_t sz = isDir ? 0 : f.size();
                f.close();
                int sl = fname.lastIndexOf('/');
                String shortName = (sl >= 0) ? fname.substring(sl + 1) : fname;
                JsonObject e = entries.add<JsonObject>();
                e["name"]  = shortName;
                e["path"]  = fname;
                e["isDir"] = isDir;
                e["size"]  = (int)sz;
            }
            dir.close();
        }
        String out; serializeJson(doc, out);
        req->send(200, "application/json", out);
    });

    // Delete file or empty directory
    server.on("/delete", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (req->hasParam("path", true)) {
            String p = req->getParam("path", true)->value();
            if (SD.exists(p.c_str())) {
                File f = SD.open(p.c_str());
                bool isDir = f && f.isDirectory();
                if (f) f.close();
                if (isDir) SD.rmdir(p.c_str());
                else       SD.remove(p.c_str());
            }
            if (p.startsWith(PATH_MUSIC)) musicScan();
        }
        req->send(200, "application/json", "{\"ok\":true}");
    });

    // Upload file — path URL param sets destination dir (default /Music)
    server.on("/upload", HTTP_POST,
        [](AsyncWebServerRequest* req) {
            if (g_uploadFile) g_uploadFile.close();
            musicScan();
            req->send(200, "application/json", "{\"ok\":true}");
        },
        [](AsyncWebServerRequest* req, const String& filename,
           size_t index, uint8_t* data, size_t len, bool final) {
            if (index == 0) {
                String dir = req->hasParam("path") ? req->getParam("path")->value()
                                                   : String(PATH_MUSIC);
                if (!dir.startsWith("/")) dir = "/" + dir;
                if (dir.endsWith("/") && dir.length() > 1)
                    dir = dir.substring(0, dir.length() - 1);
                String p = dir + "/" + filename;
                g_uploadFile = SD.open(p.c_str(), FILE_WRITE);
            }
            if (g_uploadFile) g_uploadFile.write(data, len);
            if (final && g_uploadFile) g_uploadFile.close();
        });

    // Download file
    server.on("/download", HTTP_GET, [](AsyncWebServerRequest* req) {
        if (!req->hasParam("path")) { req->send(400, "text/plain", "missing path"); return; }
        String p = req->getParam("path")->value();
        if (!p.startsWith("/")) p = "/" + p;
        req->send(SD, p.c_str(), "application/octet-stream", true);
    });

    server.begin();
    Serial.println("Web server started");
}

void webServerLoop() {
    // Nothing — AsyncWebServer is interrupt/task-driven.
    // Keep this stub so main.cpp can call it without knowing the internals.
}
