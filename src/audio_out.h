#pragma once
#include <Arduino.h>

// Call once at startup.
bool audioOutInit();

// Must be called from the main loop() every iteration.
void audioOutLoop();

// Start playing a file (MP3 or WAV) from SD. Non-blocking — playback runs in background.
void audioOutPlay(const char* sdPath);

// Stop whatever is playing right now.
void audioOutStop();

// True while audio is playing.
bool audioOutIsPlaying();

// Set volume from the UI range (1–100).  Saved to g_config.volume.
void audioOutSetVolume(int vol1to100);

// Play /ping.mp3 at 50% of current volume, then block until done, then restore volume.
// Call this from a background task — it is blocking but leaves the main loop free.
void audioOutPlayPing();

// Play any MP3/WAV file synchronously (blocks until done). Safe to call from a FreeRTOS task.
void audioOutPlaySync(const char* sdPath);
