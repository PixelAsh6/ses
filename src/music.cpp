#include "music.h"
#include "config.h"
#include "audio_out.h"
#include <SD.h>
#include <algorithm>

std::vector<Song> g_playlist;
int g_currentSong = -1;

static int getMp3DurationSec(const char* path, size_t fileSize) {
    static const int BR_KBPS[16] = {0,32,40,48,56,64,80,96,112,128,160,192,224,256,320,0};
    File f = SD.open(path);
    if (!f) return (int)(fileSize / 16000);
    uint8_t hdr[10];
    f.read(hdr, 10);
    if (hdr[0]=='I' && hdr[1]=='D' && hdr[2]=='3') {
        uint32_t tagSz = ((uint32_t)(hdr[6]&0x7f)<<21)|((uint32_t)(hdr[7]&0x7f)<<14)
                        |((uint32_t)(hdr[8]&0x7f)<<7)|(hdr[9]&0x7f);
        f.seek(10 + tagSz);
    } else {
        f.seek(0);
    }
    uint8_t buf[256];
    int n = (int)f.read(buf, sizeof(buf));
    f.close();
    for (int i = 0; i < n - 3; i++) {
        if (buf[i] == 0xFF && (buf[i+1] & 0xFE) == 0xFA) {
            int brIdx = (buf[i+2] >> 4) & 0x0F;
            int kbps  = BR_KBPS[brIdx];
            if (kbps > 0) return (int)(fileSize / ((size_t)kbps * 125));
        }
    }
    return (int)(fileSize / 16000);
}

void musicScan() {
    g_playlist.clear();
    File dir = SD.open(PATH_MUSIC);
    if (!dir || !dir.isDirectory()) { Serial.println("Music dir not found"); return; }

    std::vector<String> paths;
    while (true) {
        File f = dir.openNextFile();
        if (!f) break;
        if (!f.isDirectory()) {
            String p = f.name();
            if (!p.startsWith("/")) p = String(PATH_MUSIC) + "/" + p;
            if (p.endsWith(".mp3") || p.endsWith(".MP3")) paths.push_back(p);
        }
        f.close();
    }
    dir.close();

    std::sort(paths.begin(), paths.end());

    for (auto& p : paths) {
        Song s;
        s.path = p;
        int sl = p.lastIndexOf('/');
        s.name = (sl >= 0) ? p.substring(sl + 1) : p;
        File f2 = SD.open(p.c_str());
        size_t sz = f2 ? f2.size() : 0;
        if (f2) f2.close();
        s.duration_s = getMp3DurationSec(p.c_str(), sz);
        g_playlist.push_back(s);
    }
    Serial.printf("Music scan: %d songs\n", (int)g_playlist.size());
}

// Count how many words from query appear in target (case-insensitive).
static int wordOverlap(const String& query, const String& target) {
    String q = query; q.toLowerCase();
    String t = target; t.toLowerCase();
    int count = 0, i = 0;
    while (i < (int)q.length()) {
        while (i < (int)q.length() && !isAlphaNumeric(q[i])) i++;
        int ws = i;
        while (i < (int)q.length() && isAlphaNumeric(q[i])) i++;
        if (i == ws) continue;
        String word = q.substring(ws, i);
        if (word.length() >= 2 && t.indexOf(word) >= 0) count++;
    }
    return count;
}

int musicFindBest(const String& query) {
    if (g_playlist.empty()) return -1;
    int best = -1, bestScore = 0;
    for (int i = 0; i < (int)g_playlist.size(); i++) {
        int sc = wordOverlap(query, g_playlist[i].name);
        if (sc > bestScore) { bestScore = sc; best = i; }
    }
    return (bestScore > 0) ? best : -1;
}

void musicPlayCurrent() {
    if (g_currentSong < 0 || g_currentSong >= (int)g_playlist.size()) return;
    audioOutPlay(g_playlist[g_currentSong].path.c_str());
}

void musicNext() {
    if (g_playlist.empty()) return;
    g_currentSong = (g_currentSong + 1) % (int)g_playlist.size();
    musicPlayCurrent();
}

void musicPrev() {
    if (g_playlist.empty()) return;
    g_currentSong = (g_currentSong - 1 + (int)g_playlist.size()) % (int)g_playlist.size();
    musicPlayCurrent();
}

String musicCurrentName() {
    if (g_currentSong < 0 || g_currentSong >= (int)g_playlist.size()) return "None";
    return g_playlist[g_currentSong].name;
}
