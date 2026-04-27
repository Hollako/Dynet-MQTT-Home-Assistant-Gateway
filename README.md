# Dynet-MQTT-Home-Assistant-Gateway

Dynet MQTT Home Assistant Gateways is firmware for ESP32/ESP8266 devices that bridges a Philips Dynalite/DyNet Bus to Home Assistant via MQTT.

It listens to DyNet traffic on RS485, maps lighting channels to MQTT topics, and publishes Home Assistant discovery/state messages so Dynalite channels can appear as controllable light, Temperature, Curtains entities.

## What this project is for

- Integrating existing Dynalite lighting installations with Home Assistant.
- Sending lighting commands from Home Assistant to DyNet devices through MQTT.
- Receiving DyNet level updates and reflecting them back into Home Assistant state.
- Running on low-cost ESP hardware as a lightweight gateway.

## How it works (high level)

1. The ESP device connects to Wi-Fi (or starts fallback AP mode if no Wi-Fi credentials are configured).
2. It connects to an MQTT broker.
3. It reads/writes DyNet packets over RS485.
4. It publishes Home Assistant MQTT discovery payloads and channel state updates.
5. It subscribes to command topics and forwards commands to DyNet.

## Hardware/Software expectations

- **Board**: ESP32 or ESP8266
- **Build system**: PlatformIO
- **Framework**: Arduino
- **Dependencies**: PubSubClient, ArduinoJson, ESP Async WebServer, Async TCP library for the selected board

## Link Status LED

The status LED reflects the device state via blink patterns:

| LED pattern | Meaning |
|---|---|
| **Solid ON** | WiFi connected + MQTT connected (or no MQTT server configured) |
| **Double pulse** - ●● pause ●● pause | WiFi connected, MQTT disconnecting / reconnecting |
| **Fast blink** - 100 ms on / 100 ms off | WiFi connecting, retries in progress |
| **Slow blink** - 500 ms on / 1500 ms off | AP mode only - no WiFi or all retries exhausted |

> The LED pin and active-low logic are configurable on the **Config** page.

## Notes

- MQTT broker address/port currently use firmware defaults (to be made configurable via Web UI).
- Current Web UI is minimal and intended as a basic runtime/status endpoint.
