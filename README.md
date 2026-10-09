# SES — Smart ESP Speaker

A standalone voice assistant running on an ESP32-S3. Record a 7-second voice
command from the web UI, get it transcribed (Wit.ai), answered by an LLM
(Groq), and spoken back (Wit.ai TTS). It also plays MP3s from the SD card on
voice request ("play ...") or from the built-in music controls.

No phone app or cloud account needed to operate it day-to-day: the ESP32 hosts
its own Wi-Fi access point and web interface.

## Features

- Push-to-record voice pipeline: mic → speech-to-text → AI answer → spoken reply
- Wake word check (default `Jarvis`) before a command is acted on
- Voice-controlled music: "play \<song name\>" searches the SD playlist
- Live weather answers via `wttr.in` (no key needed), scoped to your city
- Web UI: record, transport controls, volume sliders, SD file browser,
  MP3/WAV upload, download, delete, and a settings page
- Runtime config stored on SD (`/config.json`), editable from the web UI
- Dual Wi-Fi mode: access point always up, joins home Wi-Fi when configured

## Hardware

| Part | Notes |
|---|---|
| ESP32-S3-DevKitC-1 (16 MB flash) | Main board, Arduino framework |
| INMP441 microphone | I2S digital mic |
| GY-PCM5102 DAC | I2S DAC, line-out to amplifier + speaker |
| MicroSD card module (SPI) | Required — stores config, music, recordings |
| Amplifier + speaker | Driven from the PCM5102 line output |

### Wiring (defaults in `src/config.h`)

| Signal | GPIO |
|---|---|
| SD MISO / CLK / MOSI / CS | 13 / 12 / 11 / 10 |
| I2S out BCK / LRCK / DOUT (→ PCM5102) | 4 / 5 / 6 |
| I2S in WS / SCK / SD (← INMP441) | 1 / 2 / 21 |

Audio: 16 kHz sample rate, 7 s fixed recording length.

## Software requirements

- VS Code + PlatformIO extension (or PlatformIO Core CLI)
- Libraries are resolved automatically from `platformio.ini`:
  `ESP8266Audio`, `ArduinoJson`, `ESPAsyncWebServer-esphome`

## Getting started

1. Clone the repo and open it in PlatformIO.
2. Provide compile-time Wi-Fi defaults (only needed before first flash):
   copy `src/secrets.example.h` to `src/secrets.h` and edit it —
   or keep the placeholders and set everything later from the web UI.
3. Build: `pio run`
4. Flash firmware: `pio run -t upload`
5. Flash the web UI once: `pio run -t uploadfs` (LittleFS image from `data/`)
6. Prepare the SD card (FAT32) with this layout:

   ```text
   /Music/              <- your .mp3 files
   /Recording_Input/    <- created automatically if missing
   /Recording_Output/   <- created automatically if missing
   /ping.mp3            <- short notification sound (optional)
   /config.json         <- created automatically on first save
   ```

7. Insert the SD card, power on, and join the `SmartSpeaker` access point
   (default password `12345678`). Open `http://192.168.4.1`.
8. Go to the **Settings** tab and fill in:
   - Home Wi-Fi SSID/password (for internet access)
   - Wit.ai token (speech-to-text + text-to-speech)
   - Groq API key (AI answers)
   - Wake word, city (weather), master prompt
9. Press **Record**, say e.g. *"Jarvis, what's the weather like?"*, and wait
   for the spoken reply. Try *"Jarvis, play \<song\>"* for music.

> The STA placeholder values make the first boot spend ~30 s retrying a
> nonexistent network. Set real credentials (or blank the STA fields for
> AP-only mode) to skip that.

## How it works

```text
[Record 7 s] -> WAV on SD -> Wit.ai STT -> wake word found?
      | no -> ignore (status: Ready)
      | yes -> "play X"? -> match in /Music -> play
      | yes -> other -> Groq LLM (+ live weather) -> Wit.ai TTS -> play MP3
```

Status is polled by the page via `GET /status` while a background task runs
the pipeline.

## Web API

| Endpoint | Method | Purpose |
|---|---|---|
| `/` | GET | Web UI (LittleFS `index.html`) |
| `/status` | GET | JSON: status, now playing, volumes, last heard/answer |
| `/record` | POST | Start 7 s record + full pipeline in background |
| `/volume` | POST | JSON `{volume: 1-100}` |
| `/mic-gain` | POST | JSON `{mic_gain: 1-200}` |
| `/next`, `/prev` | GET | Playlist navigation |
| `/play-pause` | POST | Toggle playback |
| `/play-file?path=` | GET | Play a specific SD file |
| `/settings-json` | GET | Current config (keys not pre-filled in UI) |
| `/settings` | POST | Save config, reconnect Wi-Fi |
| `/files-json?path=` | GET | SD directory listing as JSON |
| `/upload?path=` | POST | Multipart file upload to SD |
| `/download?path=` | GET | Download a file from SD |
| `/delete` | POST | Delete a file or empty directory |

## Configuration reference

| Key | Default | Where |
|---|---|---|
| AP SSID / password | `SmartSpeaker` / `12345678` | `config.cpp` → Settings page |
| STA SSID / password | `YOUR_WIFI_SSID` / `YOUR_WIFI_PASSWORD` | `secrets.h` → Settings page |
| Wit.ai token | empty | Settings page (stored on SD) |
| Groq API key | empty | Settings page (stored on SD) |
| Wake word | `Jarvis` | Settings page |
| City | `YOUR_CITY` | Settings page |
| Master prompt | short spoken-answer prompt | Settings page |
| Volume / mic gain | 30 / 100 | Sliders or Settings page |

## Project structure

```text
.  (repo root)
  platformio.ini       board, partitions, libraries
  partitions.csv       nvs / factory app 8 MB / littlefs ~8 MB
  data/index.html      web UI (flashed via uploadfs)
  src/
    main.cpp           boot sequence
    config.*           pins, defaults, SD load/save
    secrets.example.h  template for gitignored secrets.h
    wifi_manager.*     AP + STA connection
    web_server.*       HTTP API + voice pipeline task
    audio_in.*         INMP441 recording to WAV
    audio_out.*        MP3/WAV playback via PCM5102
    music.*            playlist scan + voice search
    wit_ai.*           Wit.ai STT / TTS clients
    ai_handler.*       Groq chat + wttr.in weather
    sd_manager.*       SD init + folder setup
```

## Privacy notes

- API keys and Wi-Fi passwords live in `src/secrets.h` (gitignored) and
  `/config.json` on the SD card — never in committed source.
- Voice recordings stay on the SD card (`/Recording_Input`,
  `/Recording_Output`). Wipe the card before giving the hardware away.
- The Settings page shows stored secrets in plaintext to anyone connected —
  don't expose the access point to untrusted users.

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| Hangs at boot, "SD card not found" | SD wiring / CS pin / card not FAT32 |
| "LittleFS not mounted" | Run `pio run -t uploadfs` once |
| "STT failed or nothing heard" | Missing Wit.ai token, no internet (STA), mic wiring |
| "Groq API key not configured" | Set key in Settings |
| Slow first boot (~30 s stall) | STA placeholder unreachable — set real Wi-Fi |
| Choppy audio with Wi-Fi on | Known ESP32 I2S/Wi-Fi coexistence tradeoff; playback already lowers TX power |

## License

MIT — see [LICENSE](LICENSE). Anyone may use, modify, and share this code as
long as the copyright notice is kept.
