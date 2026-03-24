#!/usr/bin/env python3
"""
Flipper Switch Controller - Example Python Client

Demonstrates how to connect to the ESP32 WiFi bridge and send
controller commands to the Flipper Zero / Nintendo Switch.

Usage:
    pip install websockets
    python python_client.py

This connects to the ESP32 bridge via WebSocket and provides a simple
interactive controller. You can use this as a starting point for building
your own automation scripts.

Requirements:
    - ESP32 bridge running and connected to WiFi
    - Flipper Zero running Switch Controller FAP
    - Both connected via UART (GPIO 13/14)
    - Switch connected to Flipper via USB
"""

import asyncio
import sys

try:
    import websockets
except ImportError:
    print("ERROR: websockets library required. Install with:")
    print("  pip install websockets")
    sys.exit(1)


# ============================================================
# Configuration
# ============================================================

# Default WebSocket URL -- matches ESP32 bridge mDNS hostname
WS_URL = "ws://switchcontroller.local:81/ws"

# Heartbeat interval (seconds) -- keeps the connection alive
HEARTBEAT_INTERVAL = 2.0


# ============================================================
# Protocol Commands
# ============================================================

def cmd_press(buttons: str, duration_ms: int = 100) -> str:
    """Press button(s) for a duration, then auto-release.

    Buttons: A, B, X, Y, L, R, ZL, ZR, UP, DOWN, LEFT, RIGHT,
             PLUS, MINUS, HOME, LSTICK, RSTICK
    Multiple single-char buttons can be combined: "ABXY"
    """
    return f"P:{buttons}:{duration_ms}"


def cmd_release(buttons: str = "ALL") -> str:
    """Release button(s). Use "ALL" to release everything."""
    return f"R:{buttons}"


def cmd_stick(stick: str, x: int, y: int, duration_ms: int = 500) -> str:
    """Set stick position for a duration, then return to center.

    stick: "L" for left stick, "R" for right stick
    x, y: 0-4095, center = 2048
    """
    return f"S:{stick}:{x}:{y}:{duration_ms}"


def cmd_macro(name: str) -> str:
    """Execute a predefined macro.

    Built-in macros: SOFT_RESET, PRESS_A, PRESS_B, RUN_AWAY
    """
    return f"M:{name}"


def cmd_status() -> str:
    """Query the Flipper's current status."""
    return "Q:STATUS"


def cmd_heartbeat() -> str:
    """Send heartbeat ping (keeps connection alive)."""
    return "H:PING"


# ============================================================
# WebSocket Client
# ============================================================

class SwitchController:
    """WebSocket client for the Flipper Switch Controller."""

    def __init__(self, url: str = WS_URL):
        self.url = url
        self.ws = None
        self._heartbeat_task = None

    async def connect(self):
        """Connect to the ESP32 WebSocket bridge."""
        print(f"Connecting to {self.url}...")
        self.ws = await websockets.connect(self.url)
        print("Connected!")

        # Start heartbeat task
        self._heartbeat_task = asyncio.create_task(self._heartbeat_loop())

    async def disconnect(self):
        """Disconnect and release all buttons."""
        if self._heartbeat_task:
            self._heartbeat_task.cancel()
        if self.ws:
            await self.send(cmd_release("ALL"))
            await self.ws.close()
        print("Disconnected.")

    async def send(self, command: str) -> str | None:
        """Send a command and wait for the response."""
        if not self.ws:
            raise RuntimeError("Not connected")

        await self.ws.send(command)

        try:
            response = await asyncio.wait_for(self.ws.recv(), timeout=2.0)
            return response.strip()
        except asyncio.TimeoutError:
            return None

    async def press(self, buttons: str, duration_ms: int = 100):
        """Press button(s) for a duration."""
        resp = await self.send(cmd_press(buttons, duration_ms))
        print(f"  Press {buttons} ({duration_ms}ms) -> {resp}")

    async def release(self, buttons: str = "ALL"):
        """Release button(s)."""
        resp = await self.send(cmd_release(buttons))
        print(f"  Release {buttons} -> {resp}")

    async def stick(self, stick: str, x: int, y: int, duration_ms: int = 500):
        """Set stick position."""
        resp = await self.send(cmd_stick(stick, x, y, duration_ms))
        print(f"  Stick {stick} ({x},{y}) {duration_ms}ms -> {resp}")

    async def macro(self, name: str):
        """Execute a macro."""
        resp = await self.send(cmd_macro(name))
        print(f"  Macro {name} -> {resp}")

    async def status(self):
        """Query status."""
        resp = await self.send(cmd_status())
        print(f"  Status -> {resp}")
        return resp

    async def _heartbeat_loop(self):
        """Background heartbeat to keep connection alive."""
        try:
            while True:
                await asyncio.sleep(HEARTBEAT_INTERVAL)
                if self.ws:
                    await self.ws.send(cmd_heartbeat())
                    try:
                        await asyncio.wait_for(self.ws.recv(), timeout=1.0)
                    except asyncio.TimeoutError:
                        pass
        except asyncio.CancelledError:
            pass


# ============================================================
# Demo: Interactive Controller
# ============================================================

async def demo_button_sequence(ctrl: SwitchController):
    """Demo: Press some buttons in sequence."""
    print("\n--- Demo: Button Sequence ---")

    # Press A button
    await ctrl.press("A", 100)
    await asyncio.sleep(0.5)

    # Press D-pad down
    await ctrl.press("DOWN", 100)
    await asyncio.sleep(0.5)

    # Press A again
    await ctrl.press("A", 100)
    await asyncio.sleep(0.5)

    # Move left stick up
    await ctrl.stick("L", 2048, 4095, 500)
    await asyncio.sleep(1.0)

    # Execute soft reset macro (A+B+X+Y)
    await ctrl.macro("SOFT_RESET")
    await asyncio.sleep(1.0)

    print("--- Demo complete ---\n")


async def interactive_mode(ctrl: SwitchController):
    """Interactive command mode."""
    print("\n=== Interactive Mode ===")
    print("Commands:")
    print("  a/b/x/y       - Press button (100ms)")
    print("  up/down/l/r    - Press D-pad/shoulder")
    print("  zl/zr          - Press triggers")
    print("  plus/minus     - Press +/-")
    print("  home           - Press HOME")
    print("  sr             - Macro: SOFT_RESET (A+B+X+Y)")
    print("  run            - Macro: RUN_AWAY")
    print("  ls <x> <y>     - Left stick (0-4095)")
    print("  rs <x> <y>     - Right stick (0-4095)")
    print("  status         - Query status")
    print("  release        - Release all buttons")
    print("  demo           - Run button sequence demo")
    print("  quit           - Exit")
    print()

    while True:
        try:
            line = await asyncio.get_event_loop().run_in_executor(
                None, lambda: input("switch> ").strip().lower()
            )
        except (EOFError, KeyboardInterrupt):
            break

        if not line:
            continue

        parts = line.split()
        cmd = parts[0]

        try:
            if cmd in ("a", "b", "x", "y"):
                await ctrl.press(cmd.upper(), 100)
            elif cmd in ("up", "down", "left", "right"):
                await ctrl.press(cmd.upper(), 100)
            elif cmd in ("l", "r"):
                await ctrl.press(cmd.upper(), 100)
            elif cmd in ("zl", "zr"):
                await ctrl.press(cmd.upper(), 100)
            elif cmd in ("plus", "+"):
                await ctrl.press("PLUS", 100)
            elif cmd in ("minus", "-"):
                await ctrl.press("MINUS", 100)
            elif cmd == "home":
                await ctrl.press("HOME", 100)
            elif cmd == "sr":
                await ctrl.macro("SOFT_RESET")
            elif cmd == "run":
                await ctrl.macro("RUN_AWAY")
            elif cmd == "ls" and len(parts) == 3:
                x, y = int(parts[1]), int(parts[2])
                await ctrl.stick("L", x, y, 500)
            elif cmd == "rs" and len(parts) == 3:
                x, y = int(parts[1]), int(parts[2])
                await ctrl.stick("R", x, y, 500)
            elif cmd == "status":
                await ctrl.status()
            elif cmd == "release":
                await ctrl.release("ALL")
            elif cmd == "demo":
                await demo_button_sequence(ctrl)
            elif cmd in ("quit", "exit", "q"):
                break
            else:
                print(f"  Unknown command: {cmd}")
        except Exception as e:
            print(f"  Error: {e}")

    print("Exiting interactive mode.")


# ============================================================
# Main
# ============================================================

async def main():
    import argparse
    parser = argparse.ArgumentParser(
        description="Flipper Switch Controller - Python Client"
    )
    parser.add_argument(
        "--url", default=WS_URL,
        help=f"WebSocket URL (default: {WS_URL})"
    )
    parser.add_argument(
        "--demo", action="store_true",
        help="Run button sequence demo then exit"
    )
    args = parser.parse_args()

    ctrl = SwitchController(url=args.url)

    try:
        await ctrl.connect()
        await ctrl.status()

        if args.demo:
            await demo_button_sequence(ctrl)
        else:
            await interactive_mode(ctrl)
    except websockets.exceptions.ConnectionClosed:
        print("Connection lost!")
    except ConnectionRefusedError:
        print(f"Could not connect to {args.url}")
        print("Make sure the ESP32 bridge is running and on your network.")
    finally:
        await ctrl.disconnect()


if __name__ == "__main__":
    asyncio.run(main())
