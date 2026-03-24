// ============================================================
// Flipper Switch Controller - ESP32-S2 WiFi Bridge
// ============================================================
// Transparent WebSocket-to-UART bridge for Flipper Zero control.
// The ESP32 does NOT interpret commands -- it forwards bytes
// between a PC/phone (WebSocket client) and the Flipper (UART).
//
// Arduino IDE Board Settings:
//   Board: ESP32S2 Dev Module
//   CPU Frequency: 240MHz (WiFi)
//   Flash Size: 4MB
//   Partition Scheme: Default 4MB with spiffs
//   USB CDC On Boot: Enabled
//   Upload Speed: 921600
//
// Required libraries:
//   - ESPAsyncWebServer (v3.x) from https://github.com/ESP32Async/ESPAsyncWebServer
//   - AsyncTCP (dependency, installed automatically)
//
// SETUP:
//   1. Copy wifi_config.example.h to wifi_config.h
//   2. Edit wifi_config.h with your WiFi credentials
//   3. Flash to ESP32-S2
// ============================================================

#include <WiFi.h>
#include <ESPmDNS.h>
#include <ESPAsyncWebServer.h>
#include "wifi_config.h"

// --- UART configuration (ESP32-S2 -> Flipper Zero) ---
// On the official Flipper WiFi dev board, these GPIO pins are hardwired:
//   ESP32-S2 GPIO43 (TX) -> Flipper GPIO pin 14 (RX)
//   ESP32-S2 GPIO44 (RX) <- Flipper GPIO pin 13 (TX)
#define FLIPPER_UART_RX 44
#define FLIPPER_UART_TX 43
#define FLIPPER_BAUD    115200

// --- Heartbeat tracking ---
// If no message arrives for HEARTBEAT_TIMEOUT_MS, the ESP32 assumes the
// PC is disconnected and sends R:ALL to the Flipper to release all buttons.
unsigned long lastPcHeartbeat = 0;
bool pcConnected = false;
const unsigned long HEARTBEAT_TIMEOUT_MS = 5000;

// --- WiFi watchdog ---
unsigned long lastWifiCheck = 0;
const unsigned long WIFI_WATCHDOG_MS = 30000;
unsigned long wifiLostSince = 0;

// --- WebSocket server ---
AsyncWebServer server(WS_PORT);
AsyncWebSocket ws("/ws");

// ============================================================
// WebSocket event handler
// ============================================================
void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
               AwsEventType type, void* arg, uint8_t* data, size_t len) {
    switch (type) {
        case WS_EVT_CONNECT:
            Serial.printf("WS client #%u connected from %s\n",
                          client->id(), client->remoteIP().toString().c_str());
            pcConnected = true;
            lastPcHeartbeat = millis();
            break;

        case WS_EVT_DISCONNECT:
            Serial.printf("WS client #%u disconnected\n", client->id());
            pcConnected = false;
            // Fail-safe: release all buttons when client disconnects
            Serial1.println("R:ALL");
            Serial.println("Sent R:ALL to Flipper (disconnect fail-safe)");
            break;

        case WS_EVT_DATA: {
            AwsFrameInfo* info = (AwsFrameInfo*)arg;
            if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
                lastPcHeartbeat = millis();

                // Forward to Flipper UART byte-for-byte (transparent bridge)
                Serial1.write(data, len);

                // Ensure newline termination
                if (len == 0 || data[len - 1] != '\n') {
                    Serial1.write('\n');
                }
            }
            break;
        }

        case WS_EVT_PONG:
            break;

        case WS_EVT_ERROR:
            Serial.printf("WS error on client #%u\n", client->id());
            break;
    }
}

// ============================================================
// setup()
// ============================================================
void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n=== Flipper Switch Controller - WiFi Bridge ===");

    // --- Flipper UART ---
    Serial1.setRxBufferSize(1024);
    Serial1.begin(FLIPPER_BAUD, SERIAL_8N1, FLIPPER_UART_RX, FLIPPER_UART_TX);
    Serial1.setTimeout(50);
    Serial.println("Flipper UART initialized (115200 baud, GPIO43/44)");

    // --- WiFi connection ---
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.printf("Connecting to WiFi: %s", WIFI_SSID);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 40) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\nConnected! IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("\nWiFi connection failed! Restarting...");
        ESP.restart();
    }

    // --- mDNS registration ---
    if (MDNS.begin(MDNS_HOSTNAME)) {
        MDNS.addService("ws", "tcp", WS_PORT);
        Serial.printf("mDNS: %s.local\n", MDNS_HOSTNAME);
    } else {
        Serial.println("mDNS registration failed!");
    }

    // --- WebSocket server ---
    ws.onEvent(onWsEvent);
    server.addHandler(&ws);
    server.begin();
    Serial.printf("WebSocket server started on port %d\n", WS_PORT);
    Serial.printf("Connect to: ws://%s.local:%d/ws\n", MDNS_HOSTNAME, WS_PORT);
    Serial.println("=== Bridge ready ===");
}

// ============================================================
// loop()
// ============================================================
void loop() {
    // 1. Forward Flipper UART responses to WebSocket client
    if (Serial1.available()) {
        String line = Serial1.readStringUntil('\n');
        if (line.length() > 0 && ws.count() > 0) {
            ws.textAll(line);
        }
    }

    // 2. Heartbeat timeout check
    if (pcConnected && (millis() - lastPcHeartbeat > HEARTBEAT_TIMEOUT_MS)) {
        pcConnected = false;
        Serial.println("PC heartbeat timeout! Releasing all buttons.");
        Serial1.println("R:ALL");
        ws.closeAll();
    }

    // 3. Client cleanup and WiFi watchdog
    ws.cleanupClients(1);

    if (WiFi.status() != WL_CONNECTED) {
        if (wifiLostSince == 0) {
            wifiLostSince = millis();
            Serial.println("WiFi disconnected, waiting for reconnect...");
        } else if (millis() - wifiLostSince > WIFI_WATCHDOG_MS) {
            Serial.println("WiFi lost for 30s, restarting ESP32...");
            ESP.restart();
        }
    } else {
        wifiLostSince = 0;
    }
}
