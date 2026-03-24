#pragma once

// ============================================================
// WiFi Configuration for the ESP32 Bridge
// ============================================================
// SETUP:
//   1. Copy this file to wifi_config.h
//   2. Edit the values below with your actual WiFi credentials
//   3. Flash to ESP32-S2
//
// IMPORTANT: Do NOT commit wifi_config.h to version control!
// ============================================================

const char* WIFI_SSID     = "YourWiFiName";
const char* WIFI_PASSWORD = "YourWiFiPassword";

// mDNS hostname -- the ESP32 will be reachable as "switchcontroller.local"
const char* MDNS_HOSTNAME = "switchcontroller";

// WebSocket server port (port 80 reserved for potential future web UI)
const uint16_t WS_PORT = 81;
