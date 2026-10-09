#pragma once
#include <Arduino.h>
#include <vector>

struct Song {
    String name;        // filename only, e.g. "Artist - Song.mp3"
    String path;        // full SD path,  e.g. "/Music/Artist - Song.mp3"
    int    duration_s;  // estimated from file size assuming 128 kbps
};

extern std::vector<Song> g_playlist;
extern int g_currentSong;  // index into g_playlist, -1 = nothing loaded

// Scan /Music on SD and populate g_playlist (sorted alphabetically).
void musicScan();

// Find the song whose name shares the most words with query.
// Returns index into g_playlist, or -1 if nothing matches.
int musicFindBest(const String& query);

// Play the current song (g_currentSong must be valid).
void musicPlayCurrent();

// Advance to next song (wraps) and play it.
void musicNext();

// Go to previous song (wraps) and play it.
void musicPrev();

// Current song filename, or "None" if nothing loaded.
String musicCurrentName();
