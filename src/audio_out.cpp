#include "audio_out.h"
#include "config.h"
#include <SD.h>
#include <AudioGeneratorMP3.h>
#include <AudioGeneratorWAV.h>
#include <AudioFileSourceSD.h>
#include <AudioOutputI2S.h>
#include <esp_wifi.h>
#include <driver/gpio.h>

static AudioOutputI2S*    _out  = nullptr;
static AudioGeneratorMP3* _mp3  = nullptr;
static AudioGeneratorWAV* _wav  = nullptr;
static AudioFileSourceSD* _src  = nullptr;
static float              _gain = 1.0f;

// When true, the background task owns the audio loop (ping playback).
// main loop's audioOutLoop() returns immediately to avoid race condition.
static volatile bool _locked = false;

// Map 1–100 → 0.02–2.0 gain  (50 = unity 1.0, 100 = 2x, 1 = nearly silent)
static float volToGain(int v) {
    return constrain(v, 1, 100) / 50.0f;
}

static void stopAll() {
    if (_mp3) { if (_mp3->isRunning()) _mp3->stop(); delete _mp3; _mp3 = nullptr; }
    if (_wav) { if (_wav->isRunning()) _wav->stop(); delete _wav; _wav = nullptr; }
    if (_src) { delete _src; _src = nullptr; }
    esp_wifi_set_max_tx_power(80);  // restore full TX power once playback ends
}

bool audioOutInit() {
    // EXTERNAL_I2S = use dedicated I2S pins (not the internal DAC)
    // dma_buf_count=16 (lib default 8, fixed dma_buf_len=128) — bigger DMA buffer
    // absorbs WiFi-TX timing glitches. Lib has no SetBufferCountAndSize(); this
    // ctor arg is the only public knob for buffer depth.
    _out = new AudioOutputI2S(0, AudioOutputI2S::EXTERNAL_I2S, 16);
    _out->SetPinout(PIN_I2S_BCK, PIN_I2S_LRCK, PIN_I2S_DOUT);
    // Max drive strength on the I2S lines — sharper edges resist WiFi-TX coupled noise
    gpio_set_drive_capability((gpio_num_t)PIN_I2S_BCK,  GPIO_DRIVE_CAP_3);
    gpio_set_drive_capability((gpio_num_t)PIN_I2S_LRCK, GPIO_DRIVE_CAP_3);
    gpio_set_drive_capability((gpio_num_t)PIN_I2S_DOUT, GPIO_DRIVE_CAP_3);
    _gain = volToGain(g_config.volume);
    _out->SetGain(_gain);
    return (_out != nullptr);
}

void audioOutLoop() {
    if (_locked) return;  // background ping task owns the loop right now
    if (_mp3 && _mp3->isRunning()) {
        if (!_mp3->loop()) stopAll();
    } else if (_wav && _wav->isRunning()) {
        if (!_wav->loop()) stopAll();
    }
}

void audioOutPlay(const char* path) {
    stopAll();
    esp_wifi_set_max_tx_power(40);  // ~10dBm during playback — cuts WiFi TX EMI on I2S lines
    _src = new AudioFileSourceSD(path);
    String p(path);
    p.toLowerCase();
    if (p.endsWith(".mp3")) {
        _mp3 = new AudioGeneratorMP3();
        _mp3->begin(_src, _out);
    } else {
        _wav = new AudioGeneratorWAV();
        _wav->begin(_src, _out);
    }
}

void audioOutStop() {
    // Lock out audioOutLoop() before deleting _mp3/_wav/_src — without this,
    // a concurrent _mp3->loop() call from the main loop() task can race with
    // delete here (use-after-free / heap corruption), same hazard guarded
    // against in audioOutPlayPing()/audioOutPlaySync().
    _locked = true;
    delay(20);  // allow any in-progress audioOutLoop() iteration to finish
    stopAll();
    _locked = false;
}

bool audioOutIsPlaying() {
    return (_mp3 && _mp3->isRunning()) || (_wav && _wav->isRunning());
}

void audioOutSetVolume(int vol1to100) {
    g_config.volume = constrain(vol1to100, 1, 100);
    _gain = volToGain(g_config.volume);
    if (_out) _out->SetGain(_gain);
}

void audioOutPlaySync(const char* sdPath) {
    _locked = true;
    delay(20);

    stopAll();

    _src = new AudioFileSourceSD(sdPath);
    String p(sdPath); p.toLowerCase();
    if (p.endsWith(".mp3")) {
        _mp3 = new AudioGeneratorMP3();
        _mp3->begin(_src, _out);
    } else {
        _wav = new AudioGeneratorWAV();
        _wav->begin(_src, _out);
    }

    unsigned long deadline = millis() + 120000UL;
    while (millis() < deadline) {
        bool running = (_mp3 && _mp3->isRunning()) || (_wav && _wav->isRunning());
        if (!running) break;
        if (_mp3 && _mp3->isRunning()) { if (!_mp3->loop()) break; }
        else if (_wav && _wav->isRunning()) { if (!_wav->loop()) break; }
        delay(1);
    }

    stopAll();
    _locked = false;
}

void audioOutPlayPing() {
    if (!SD.exists(PATH_PING)) {
        Serial.println("ping.mp3 missing, skipping");
        return;
    }

    // Lock: tell main loop's audioOutLoop() to stand down
    _locked = true;
    delay(20);  // allow any in-progress audioOutLoop() iteration to finish

    stopAll();
    _out->SetGain(_gain / 2.0f);

    _src = new AudioFileSourceSD(PATH_PING);
    _mp3 = new AudioGeneratorMP3();
    _mp3->begin(_src, _out);

    // Background task drives the audio loop for the duration of the ping
    unsigned long deadline = millis() + 5000;
    while (_mp3->isRunning() && millis() < deadline) {
        _mp3->loop();
        delay(1);
    }

    stopAll();
    _out->SetGain(_gain);  // restore output volume
    _locked = false;
}
