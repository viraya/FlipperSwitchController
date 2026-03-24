# Flipper Zero Switch Controller

**Turn your Flipper Zero into a Nintendo Switch Pro Controller.**

The first working implementation of a Flipper Zero emulating a Nintendo Switch Pro Controller over USB. The Switch sees the Flipper as a real Pro Controller — complete with handshake, SPI flash emulation, IMU data, and 125Hz input reports.

## Two Ways to Use

### Option 1: Flipper Only (No Extra Hardware)

Use your Flipper Zero's buttons to directly control the Switch. No ESP32, no WiFi, no computer needed.

| Flipper Button | Switch Action |
|----------------|---------------|
| D-pad          | D-pad         |
| OK             | A button      |
| Back (short)   | B button      |
| Back (long)    | Exit app      |

**What you need:**
- Flipper Zero (Momentum firmware)
- USB-C cable
- Nintendo Switch dock

### Option 2: WiFi Remote Control (ESP32 Bridge)

Control the Switch remotely from any device on your network. Send button commands over WiFi through an ESP32 bridge that relays to the Flipper via UART. Build automation scripts, remote controllers, or any custom tool.

```
Your PC/Phone ──WiFi──> ESP32 ──UART──> Flipper Zero ──USB──> Nintendo Switch
                WebSocket        GPIO 13/14        Pro Controller
```

**What you need:**
- Flipper Zero (Momentum firmware)
- ESP32-S2 WiFi dev board (the Flipper WiFi devboard works perfectly)
- USB-C cable + Nintendo Switch dock
- A computer on the same WiFi network

---

## Quick Start

### Step 1: Flash the Flipper FAP

**Prerequisites:** [ufbt](https://github.com/flipperdevices/flipperzero-ufbt) toolchain with Momentum firmware SDK.

```bash
cd flipper/switch_controller
ufbt                    # Build the FAP
ufbt install            # Install to Flipper SD card
```

The app appears under **GPIO > Switch Controller** on your Flipper.

### Step 2: Connect to Switch

1. Plug Flipper into a Switch dock via USB-C
2. Open **Switch Controller** on the Flipper
3. Go to **USB Debug** — you should see the handshake progress (HS:1 → HS:4 → OK)
4. On the Switch, go to **Controllers > Change Grip/Order**
5. Press **OK** on the Flipper (sends L+R) to register the controller
6. Done! The Flipper is now a Pro Controller

### Step 3a: Manual Mode (Flipper Only)

1. Select **Manual Mode (Flipper)** from the menu
2. Use D-pad and OK/Back to control the Switch
3. Long-press Back to exit

### Step 3b: Remote Mode (WiFi via ESP32)

#### Flash the ESP32 Bridge

1. Copy `esp32_bridge/wifi_config.example.h` to `esp32_bridge/wifi_config.h`
2. Edit `wifi_config.h` with your WiFi SSID and password
3. Open `esp32_bridge/esp32_bridge.ino` in Arduino IDE
4. Board settings:
   - Board: **ESP32S2 Dev Module**
   - USB CDC On Boot: **Enabled**
   - Upload Speed: **921600**
5. Install library: **ESPAsyncWebServer** (v3.x) from [ESP32Async/ESPAsyncWebServer](https://github.com/ESP32Async/ESPAsyncWebServer)
6. Flash to ESP32

#### Wire ESP32 to Flipper

The Flipper WiFi devboard is pre-wired. For other ESP32-S2 boards:

| ESP32-S2 | Flipper Zero |
|----------|-------------|
| GPIO 43 (TX) | GPIO pin 14 (RX) |
| GPIO 44 (RX) | GPIO pin 13 (TX) |
| GND | GND |

#### Connect from PC

```bash
cd examples
pip install -r requirements.txt
python python_client.py
```

The client connects to `ws://switchcontroller.local:81/ws` and gives you an interactive shell:

```
switch> a                    # Press A
switch> down                 # Press D-pad down
switch> sr                   # Soft Reset (A+B+X+Y)
switch> ls 2048 4095         # Left stick full up
switch> status               # Query Flipper status
switch> demo                 # Run button sequence demo
```

---

## UART Protocol Reference

The Flipper accepts newline-delimited text commands at **115200 baud**. Use these to build your own control software in any language.

### Commands (PC → Flipper)

| Command | Format | Example | Description |
|---------|--------|---------|-------------|
| Press | `P:buttons:duration_ms` | `P:A:100` | Press button(s) for duration, then auto-release |
| Release | `R:buttons` | `R:ALL` | Release specific buttons or ALL |
| Stick | `S:stick:x:y:duration_ms` | `S:L:0:2048:500` | Set stick position (0-4095, center=2048) |
| Macro | `M:name` | `M:SOFT_RESET` | Execute predefined macro |
| Status | `Q:STATUS` | `Q:STATUS` | Query current status |
| Heartbeat | `H:PING` | `H:PING` | Keep-alive ping |
| Update | `U:field:value` | `U:STATE:Active` | Update Flipper display |

### Button Names

Single buttons: `A`, `B`, `X`, `Y`, `L`, `R`

Multi-char buttons: `UP`, `DOWN`, `LEFT`, `RIGHT`, `ZL`, `ZR`, `PLUS`, `MINUS`, `HOME`, `LSTICK`, `RSTICK`

Combine single-char buttons: `ABXY` presses A, B, X, Y simultaneously.

### Stick Values

- Range: **0 to 4095** (12-bit)
- Center: **2048**
- Full up: `S:L:2048:4095:500`
- Full down: `S:L:2048:0:500`
- Full left: `S:L:0:2048:500`
- Full right: `S:L:4095:2048:500`

### Responses (Flipper → PC)

| Response | Meaning |
|----------|---------|
| `OK` | Command acknowledged |
| `OK:STATUS:mode=idle:usb=1` | Status response |
| `H:PONG` | Heartbeat reply |
| `ERR:BUSY` | Engine busy (macro running) |
| `ERR:UNKNOWN_CMD` | Unrecognized command |
| `ERR:OVERFLOW` | Command too long |

### Built-in Macros

| Name | Action | Duration |
|------|--------|----------|
| `SOFT_RESET` | A+B+X+Y | 200ms |
| `PRESS_A` | A | 100ms |
| `PRESS_B` | B | 100ms |
| `RUN_AWAY` | DOWN → RIGHT → A | ~460ms |

---

## Architecture

```
+----------------------------------+
|  Your Application (PC/Phone)     |  <-- You build this!
|  Python, Node.js, C#, anything   |
+----------------------------------+
        | WebSocket (ws://switchcontroller.local:81/ws)
        v
+----------------------------------+
|  ESP32-S2 WiFi Bridge            |  Transparent relay (no logic)
|  WebSocket server on port 81     |  Fail-safe: R:ALL on disconnect
+----------------------------------+
        | UART 115200 baud (GPIO 43/44 → Flipper 13/14)
        v
+----------------------------------+
|  Flipper Zero FAP                |
|  ┌──────────────────────────┐    |
|  │ UART Receiver            │    |  ISR-safe async RX → worker thread
|  │ Button Engine            │    |  Timed press/release, macro sequences
|  │ USB Pro Controller       │    |  0x80 handshake, 0x30 reports @ 125Hz
|  │ GUI (SceneManager)       │    |  Menu, controller view, USB debug
|  └──────────────────────────┘    |
+----------------------------------+
        | USB HID (VID 0x057E, PID 0x2009)
        v
+----------------------------------+
|  Nintendo Switch                 |  Thinks it's a real Pro Controller
+----------------------------------+
```

### Key Technical Details

- **USB VID/PID**: `0x057E:0x2009` (Nintendo Pro Controller)
- **Handshake**: Full 0x80 subcommand sequence (0x01→0x04)
- **Subcommands**: Device info (0x02), SPI flash reads (0x10), IMU enable (0x40), vibration (0x48), LEDs (0x30), and more
- **SPI Flash**: Virtual flash with calibration data, stick params, and controller colors
- **IMU Data**: 3 frames of accelerometer + gyroscope per report (stationary values, prevents orientation math crashes)
- **Input Reports**: 64-byte 0x30 reports at 125Hz with buttons, 12-bit analog sticks, and IMU
- **Fail-safes**: Heartbeat timeout releases all buttons, WiFi loss auto-pauses, disconnect sends R:ALL

---

## Adding Custom Macros

Edit `flipper/switch_controller/engine/sequences.c` to add your own macros:

```c
/* Example: Press HOME, wait 500ms, press A */
static const SequenceStep sequence_open_menu[] = {
    {.buttons = "HOME",  .duration_ms = 100, .pause_ms = 500},
    {.buttons = "A",     .duration_ms = 100, .pause_ms = 0},
};

/* Add to the macros[] array: */
{
    .name = "OPEN_MENU",
    .steps = sequence_open_menu,
    .step_count = sizeof(sequence_open_menu) / sizeof(sequence_open_menu[0]),
},
```

Then use with `M:OPEN_MENU` over UART or from your Python script.

---

## Building from Source

### Flipper FAP

Requires [ufbt](https://github.com/flipperdevices/flipperzero-ufbt) with Momentum firmware SDK:

```bash
cd flipper/switch_controller
ufbt update --channel=dev   # Get latest SDK (if needed)
ufbt                        # Build
ufbt install                # Install to connected Flipper
```

### ESP32 Bridge

Requires Arduino IDE 2.x with ESP32 board support:

1. Install ESP32 board package by Espressif
2. Install ESPAsyncWebServer library (v3.x)
3. Select board: ESP32S2 Dev Module
4. Copy `wifi_config.example.h` → `wifi_config.h` and edit credentials
5. Upload

---

## Troubleshooting

### Switch doesn't detect the controller
- Make sure you're using a **dock** (the Switch's USB-C port alone doesn't support controller input)
- Check USB Debug screen on Flipper: `Bus:Y` means USB is connected, `HS:4 OK` means handshake passed
- Try unplugging and re-plugging the USB cable
- Go to Switch **Controllers > Change Grip/Order** and press OK on Flipper (sends L+R)

### ESP32 won't connect to WiFi
- Double-check SSID and password in `wifi_config.h`
- Make sure you're using a 2.4GHz network (ESP32-S2 doesn't support 5GHz)
- Check serial monitor (115200 baud) for connection errors
- The ESP32 auto-restarts after 30 seconds if WiFi is lost

### WebSocket connection refused
- Verify ESP32 is on the network: `ping switchcontroller.local`
- Check the correct port (default: 81)
- Try using IP address instead of mDNS hostname
- Only one client can connect at a time

### Buttons don't register on Switch
- Check the USB Debug screen: `Rpt:` counter should be increasing
- Make sure handshake completed: `HS:4 OK`
- Ensure no other USB device is using the controller slot

---

## Project Structure

```
├── flipper/switch_controller/     # Flipper Zero FAP (C)
│   ├── application.fam            # FAP manifest
│   ├── switch_controller.c        # Main app entry point
│   ├── switch_controller_i.h      # Shared types and includes
│   ├── usb/                       # Nintendo Pro Controller USB emulation
│   │   ├── switch_pro_usb.c       # Full USB HID implementation (~1000 lines)
│   │   ├── switch_pro_usb.h       # Public API
│   │   └── switch_pro_report.h    # Button state + report builder
│   ├── engine/                    # Button command automation
│   │   ├── button_engine.c        # Timed press/release, macro execution
│   │   ├── button_engine.h        # Public API
│   │   ├── sequences.c            # Macro definitions
│   │   └── sequences.h            # Macro types
│   ├── uart/                      # ESP32 UART communication
│   │   ├── uart_receiver.c        # ISR-safe async RX + worker thread
│   │   ├── uart_receiver.h        # Public API
│   │   └── uart_protocol.h       # Protocol definitions
│   ├── scenes/                    # GUI scene handlers
│   │   ├── scene_menu.c           # Mode selection menu
│   │   ├── scene_controller.c     # Controller status display
│   │   ├── scene_usb_debug.c      # USB diagnostics
│   │   └── scene_confirm_exit.c   # Exit confirmation
│   └── views/                     # Custom view implementations
│       ├── controller_view.c/h    # Controller status + manual mode input
│       └── usb_debug_view.c/h     # USB debug display
├── esp32_bridge/                  # ESP32-S2 WiFi bridge (Arduino)
│   ├── esp32_bridge.ino           # WebSocket-to-UART bridge
│   └── wifi_config.example.h      # WiFi config template
├── examples/                      # Example client code
│   ├── python_client.py           # Interactive Python controller
│   └── requirements.txt           # Python dependencies
└── README.md                      # This file
```

---

## Feature Ideas for Contributors

Here are features that would make this project even more powerful:

- **Turbo Mode** - Auto-repeat any button at configurable intervals (great for dialogue skipping, grinding)
- **SD Card Macro Playback** - Load and play input sequences from `.txt` files on the Flipper's SD card (TAS-style)
- **Recording Mode** - Record button inputs from a real controller and replay them
- **Web UI Controller** - Serve a virtual controller webpage directly from the ESP32 (control from your phone browser)
- **Bluetooth Pro Controller** - Emulate a wireless Pro Controller via BLE (eliminates the dock requirement)
- **Game Profiles** - Pre-built macro sets for popular games (Pokemon, Zelda, Animal Crossing, Splatoon)
- **Analog Stick via Flipper** - Use long-press + direction for analog stick movement in manual mode
- **Amiibo Combo** - Trigger NFC Amiibo scans while controlling the game
- **Multi-Switch Support** - Control multiple Switches from one ESP32 (tournament setups)
- **Scripting Engine** - Simple on-device scripting language for sequences without recompiling

---

## Credits & References

- [dekuNukem/Nintendo_Switch_Reverse_Engineering](https://github.com/dekuNukem/Nintendo_Switch_Reverse_Engineering) - Switch USB protocol documentation
- [ToadKing's Pro Controller gist](https://gist.github.com/ToadKing/b883a8ccfa26adcc6ba9905e75aeb4f2) - HID report descriptor reference
- [Flipper-Zero-Joycon](https://github.com/ccyyturralde/Flipper-Zero-Joycon) - Earlier Flipper Joy-Con work
- [Momentum Firmware](https://github.com/Next-Flip/Momentum-Firmware) - Custom firmware for Flipper Zero

## License

MIT License - see [LICENSE](LICENSE)
