---
name: deploy
description: Deploy lmchan firmware to an ESP32-S3 board (configure, build, flash, verify, troubleshoot).
---

# Deploy lmchan

End-to-end deploy workflow for lmchan.

## Prerequisites

### Hardware
- ESP32-S3 board (recommended 16MB Flash + 8MB PSRAM)
- USB data cable

### Software
- ESP-IDF v5.5+
- `idf.py --version` works in your shell

### Required credentials
- WiFi SSID/password
- Telegram Bot Token
- LLM API Key (Anthropic/OpenAI/OpenAI-compatible)

Optional:
- Brave Search API key
- Proxy host/port

## 1) Clone and Set Target

```bash
git clone https://github.com/memovai/lmchan.git
cd lmchan
idf.py set-target esp32s3
```

## 2) Configure Secrets

```bash
cp main/lmchan_secrets.h.example main/lmchan_secrets.h
```

Fill `main/lmchan_secrets.h`.

Minimum required fields:

```c
#define LMCHAN_SECRET_WIFI_SSID       "YourWiFi"
#define LMCHAN_SECRET_WIFI_PASS       "YourPass"
#define LMCHAN_SECRET_TG_TOKEN        "123456:ABC..."
#define LMCHAN_SECRET_API_KEY         "sk-..."
#define LMCHAN_SECRET_MODEL_PROVIDER  "anthropic"
```

## 3) Build

```bash
idf.py fullclean && idf.py build
```

## 4) Flash

```bash
idf.py -p PORT flash monitor
```

Windows example:

```bash
idf.py -p COM6 flash monitor
```

## 5) Verify Runtime

Look for logs indicating:
- app booted
- WiFi connected
- Telegram service started
- agent loop started

Then test on Telegram:
- Send `/start`
- Send `hello`
- Send `what time is it`

## Runtime CLI Essentials

```text
config_show
set_api_key <key>
set_model_provider <anthropic|openai|openai_compatible>
set_api_base <https://...>
set_api_path </v1/...>
set_allowlist <id1,id2>
show_allowlist
set_proxy <host> <port> [http|socks5]
set_heartbeat_interval <minutes>
restart
```

## Troubleshooting

- `idf.py not recognized`
  - Source ESP-IDF env first (`export.sh` / `export.ps1`)

- `Failed to connect ... No serial data received`
  - Enter download mode manually: hold `BOOT` -> tap `RST/EN` -> release BOOT after 1-2s

- `port busy / access denied`
  - Close serial tools (Arduino IDE, other monitors), retry

- `port not found`
  - Re-scan ports; COM index may change after reset/download mode

- Telegram no response
  - Check token/key/provider with `config_show`
  - Check proxy/network reachability