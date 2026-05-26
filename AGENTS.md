# AGENTS.md — Security-Shelly-Plug-S-gen1

Research project: IoT security analysis of Shelly Plug S (Gen 1, ESP8266). Italian-language docs.

## Repository structure

| Directory | Purpose |
|-----------|---------|
| `Fase1/` | Physical access, firmware extraction, credential leakage analysis |
| `Fase2/` | Red Team — malicious firmware + UDP exfiltration server |
| `Fase3/` | Blue Team — secure firmware (MQTTS + signed OTA) |
| `MITM/www/` | Fake firmware update server for MITM attack phase |

## Firmware builds

Three firmware variants, **no Makefile / platformio.ini / CMakeLists.txt** — all are Arduino `.ino` sketches:

- **Fase2 Arduino** (`Fase2/red_firmware/ShellyPlugS_3/ShellyPlugS_3.ino`) — malicious firmware with UDP telemetry exfiltration
- **Fase2 Mongoose OS** (`Fase2/ShellyPlugS_Mongoose/`) — same attack, built with `mos` CLI + `mos.yml`
- **Fase3 secure** (`Fase3/ShellyPlugS/ShellyPlugS.ino`) — MQTTS (mTLS via BearSSL), signed OTA (RSA 2048 PKCS1v15+SHA256)

Mongoose OS build: requires `mos` toolchain; config in `mos.yml`.

## OTA signing tools (Fase3)

```bash
python key.py                        # generate RSA 2048 keypair
python signature.py <firmware.bin>   # produces <name>_firmato.bin
```

Output format: original firmware + 256-byte RSA signature + 4-byte little-endian signature length.

The public key and mTLS certs are hardcoded in `ShellyPlugS.ino`.

## Python dependencies

```bash
pip install pymongo[srv] dnspython cryptography
```

## UDP telemetry server

`Fase2/udpServer.py` — listens on UDP **port 9999**, stores JSON payloads in MongoDB + CSV. Systemd unit at `udp_server.service` (env vars: `MONGO_URI`, `MONGO_DB`, `MONGO_COLLECTION`).

## Credentials found in original firmware (Fase1)

See `Fase1/conf.txt` — contains WiFi PSK, cloud AES key/IV, device PIN. All hardcoded in plaintext.

## Key files to know

- `Fase3/ShellyPlugS/private_key.pem` / `public_key.pem` — OTA signing keys (gitignored)
- `.gitignore` covers `private_key.pem`, `public_key.pem`, `.DS_Store`
- `MITM/www/shelly-plug-s-1.0/` — original firmware binary bundle (`shelly-plug-s.bin`, `fs.bin`, `rboot.bin`, `manifest.json`)
- `Fase1/aes.txt` — multiple AES key/IV pairs extracted from firmware dumps
