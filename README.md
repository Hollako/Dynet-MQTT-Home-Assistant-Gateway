# Dynet-MQTT-Home-Assistant-Gateway

This is a Gateway based on ESP devices that allow to integrate Dynalite System Devices into Home Assistant via MQTT Protocol.

## PlatformIO support

This project is now organized as a standard PlatformIO project:

- firmware sources are in `src/`
- headers are in `include/`

Firmware logic was not changed; only project/build structure was adapted.

### Quick start

1. Install PlatformIO Core.
2. Build for ESP8266 (default):
   ```bash
   pio run
   ```
3. Upload to ESP8266:
   ```bash
   pio run -t upload
   ```
4. Build for ESP32:
   ```bash
   pio run -e esp32
   ```

### Create one GitHub release with both chipset binaries

A GitHub Actions workflow is configured to build both firmware variants and publish them in a **single release** when you push a version tag.

1. Commit your code changes.
2. Create and push a version tag:
   ```bash
   git tag v1.0.0
   git push origin v1.0.0
   ```
3. The workflow will create one release `v1.0.0` with two artifacts:
   - `Dynet-MQTT-Gateway-v1.0.0-esp8266.bin`
   - `Dynet-MQTT-Gateway-v1.0.0-esp32.bin`

You can repeat the process for each new version (`v1.0.1`, `v1.1.0`, etc.).

### Notes

- LittleFS is enabled for both ESP8266 and ESP32 environments.
