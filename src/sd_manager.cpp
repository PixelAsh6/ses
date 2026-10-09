#include "sd_manager.h"
#include "config.h"
#include <SPI.h>
#include <SD.h>

bool sdInit() {
    // Assign SPI pins explicitly before SD.begin()
    SPI.begin(PIN_SD_CLK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
    if (!SD.begin(PIN_SD_CS, SPI, 4000000)) {
        Serial.println("SD init failed");
        return false;
    }
    Serial.printf("SD: %llu MB\n", SD.cardSize() / (1024 * 1024));
    sdEnsureDirs();
    return true;
}

void sdEnsureDirs() {
    const char* dirs[] = { PATH_REC_IN, PATH_REC_OUT, PATH_MUSIC };
    for (auto d : dirs) {
        if (!SD.exists(d)) SD.mkdir(d);
    }
}
