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

### Notes

- LittleFS is enabled for both ESP8266 and ESP32 environments.
