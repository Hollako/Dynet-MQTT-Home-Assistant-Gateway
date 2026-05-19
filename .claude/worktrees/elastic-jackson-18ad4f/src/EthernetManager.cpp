#include "Globals.h"

// ============================================================
//  EthernetManager — ESP32 internal Ethernet MAC (LAN8720 etc.)
//  Guard: compiled only when ETHERNET_SUPPORTED is defined AND
//         the target is ESP32.  The stub block below provides
//         no-op symbols for all other builds.
// ============================================================

#if defined(ESP32) && defined(ETHERNET_SUPPORTED)
#include <ETH.h>

static bool _ethConnected = false;

void ethSetup() {
  LOGF("[ETH] Init  phy=%u addr=%u pwr=%d mdc=%d mdio=%d\n",
       cfg.eth_phy_type, cfg.eth_phy_addr,
       (int)cfg.eth_power_pin, (int)cfg.eth_mdc_pin, (int)cfg.eth_mdio_pin);

  // Ethernet events arrive on the same WiFi event bus on ESP32
  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
    switch (event) {
      case ARDUINO_EVENT_ETH_START:
        ETH.setHostname(deviceId.c_str());
        LOGLN("[ETH] Interface started");
        break;
      case ARDUINO_EVENT_ETH_CONNECTED:
        LOGLN("[ETH] Link up");
        break;
      case ARDUINO_EVENT_ETH_GOT_IP:
        _ethConnected = true;
        ethModeActive = true;
        LOGF("[ETH] IP=%s  %u Mbps %s\n",
             ETH.localIP().toString().c_str(),
             ETH.linkSpeed(),
             ETH.fullDuplex() ? "FD" : "HD");
        break;
      case ARDUINO_EVENT_ETH_DISCONNECTED:
        _ethConnected = false;
        LOGLN("[ETH] Link down");
        break;
      case ARDUINO_EVENT_ETH_STOP:
        _ethConnected = false;
        ethModeActive = false;
        LOGLN("[ETH] Stopped");
        break;
      default:
        break;
    }
  });

  // Map cfg byte → arduino-esp32 ETH_PHY_TYPE enum
  eth_phy_type_t phyType;
  switch (cfg.eth_phy_type) {
    case 1:  phyType = ETH_PHY_IP101;   break;
    case 2:  phyType = ETH_PHY_RTL8201; break;
    case 3:  phyType = ETH_PHY_DP83848; break;
    default: phyType = ETH_PHY_LAN8720; break;  // 0 = LAN8720 (most common)
  }

  // Apply sensible defaults when pins are unset (zero-init cfg on first boot)
  int mdc  = (cfg.eth_mdc_pin  >= 0) ? (int)cfg.eth_mdc_pin  : 23;
  int mdio = (cfg.eth_mdio_pin >= 0) ? (int)cfg.eth_mdio_pin : 18;
  int pwr  = (int)cfg.eth_power_pin;  // -1 means unused — ETH.begin accepts -1

  ETH.begin(cfg.eth_phy_addr, pwr, mdc, mdio, phyType);
}

void ethLoop() {
  // Ethernet is fully event-driven; nothing to poll here.
}

bool ethConnected() {
  return _ethConnected;
}

// ============================================================
#else  // Stub — Ethernet not supported on this build target
// ============================================================

void ethSetup() {
#if !defined(ETHERNET_SUPPORTED)
  if (cfg.net_mode == NET_ETHERNET)
    LOGLN("[ETH] Ethernet mode selected but ETHERNET_SUPPORTED not defined in build flags");
#endif
}

void ethLoop() {}

bool ethConnected() { return false; }

#endif  // ESP32 && ETHERNET_SUPPORTED
