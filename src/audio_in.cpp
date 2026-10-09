#include "audio_in.h"
#include "config.h"
#include <driver/i2s.h>
#include <SD.h>

// INMP441 outputs 24-bit audio left-aligned in 32-bit I2S frames.
// Base shift of 11 maps the 32-bit raw value to the loudest useful 16-bit range.
// g_config.mic_gain (1–200, default 100) then scales linearly in PSRAM buffer.
#define MIC_BASE_SHIFT 16

static bool _inited = false;

bool audioInInit() {
    i2s_config_t cfg = {
        .mode            = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate     = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format  = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count   = 8,
        .dma_buf_len     = 512,
        .use_apll        = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk      = 0
    };
    if (i2s_driver_install(I2S_PORT_IN, &cfg, 0, NULL) != ESP_OK) return false;

    i2s_pin_config_t pins = {
        .bck_io_num   = PIN_MIC_SCK,
        .ws_io_num    = PIN_MIC_WS,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num  = PIN_MIC_SD
    };
    if (i2s_set_pin(I2S_PORT_IN, &pins) != ESP_OK) return false;

    _inited = true;
    return true;
}

// Write a WAV header to a file opened at position 0.
static void writeWavHeader(File& f, uint32_t pcmBytes) {
    const uint16_t channels    = 1;
    const uint32_t sampleRate  = SAMPLE_RATE;
    const uint16_t bitsPerSamp = 16;
    const uint32_t byteRate    = sampleRate * channels * bitsPerSamp / 8;
    const uint16_t blockAlign  = channels * bitsPerSamp / 8;
    const uint32_t chunkSize   = 36 + pcmBytes;

    f.write((const uint8_t*)"RIFF", 4);
    f.write((uint8_t*)&chunkSize,    4);
    f.write((const uint8_t*)"WAVE", 4);
    f.write((const uint8_t*)"fmt ", 4);
    uint32_t sub1 = 16; f.write((uint8_t*)&sub1, 4);
    uint16_t fmt  = 1;  f.write((uint8_t*)&fmt,  2);
    f.write((uint8_t*)&channels,    2);
    f.write((uint8_t*)&sampleRate,  4);
    f.write((uint8_t*)&byteRate,    4);
    f.write((uint8_t*)&blockAlign,  2);
    f.write((uint8_t*)&bitsPerSamp, 2);
    f.write((const uint8_t*)"data", 4);
    f.write((uint8_t*)&pcmBytes,    4);
}

bool audioInRecord(const char* outPath) {
    if (!_inited) return false;

    // Flush stale DMA data
    i2s_zero_dma_buffer(I2S_PORT_IN);

    File f = SD.open(outPath, FILE_WRITE);
    if (!f) { Serial.println("Cannot open file for recording"); return false; }

    // Write placeholder header — we'll overwrite after we know the data size
    uint32_t zero = 0;
    writeWavHeader(f, zero);

    const size_t BUF_SAMPLES = 512;

    // Static: avoids 2 KB stack allocation every call, and no heap needed
    static int32_t raw[BUF_SAMPLES];

    uint32_t totalPcm     = 0;
    uint32_t totalSamples = (uint32_t)SAMPLE_RATE * RECORD_SECS;
    uint32_t samplesRead  = 0;

    const size_t BUF_BYTES = BUF_SAMPLES * sizeof(int32_t);
    while (samplesRead < totalSamples) {
        size_t bytesRead = 0;
        i2s_read(I2S_PORT_IN, raw, BUF_BYTES, &bytesRead, portMAX_DELAY);
        size_t got = bytesRead / sizeof(int32_t);

        int gain = g_config.mic_gain;  // 1–200, 100 = unity
        for (size_t i = 0; i < got && samplesRead < totalSamples; i++, samplesRead++) {
            // Shift to 16-bit, then apply runtime gain from PSRAM-loaded buffer
            int32_t s32 = (raw[i] >> MIC_BASE_SHIFT) * gain / 100;
            int16_t s   = (int16_t)constrain(s32, -32768, 32767);
            f.write((uint8_t*)&s, 2);
            totalPcm += 2;
        }
    }

    // Rewrite header with actual data size
    f.seek(0);
    writeWavHeader(f, totalPcm);
    f.close();

    Serial.printf("Recorded %u bytes PCM to %s\n", totalPcm, outPath);
    return true;
}
