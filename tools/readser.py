#!/usr/bin/env python3
"""Minimal serial monitor for this project.

`pio device monitor` fails in non-interactive shells (no TTY). This reads the port directly,
asserts DTR (so the ESP32 USB-CDC emits), and reconnects across device resets/re-enumeration
so it can catch boot output after a RST.

    python3 tools/readser.py /dev/ttyACM0 [seconds]

Use the platformio venv python (has pyserial):
    ~/.platformio/penv/bin/python tools/readser.py /dev/ttyACM0 30
"""
import serial, sys, time

port = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyACM0"
dur = float(sys.argv[2]) if len(sys.argv) > 2 else 30.0
end = time.time() + dur
while time.time() < end:
    try:
        s = serial.Serial(port, 115200, timeout=0.2)
        s.dtr = True
        s.rts = False
        while time.time() < end:
            d = s.read(4096)
            if d:
                sys.stdout.write(d.decode(errors="replace"))
                sys.stdout.flush()
        s.close()
    except Exception:
        time.sleep(0.3)  # port gone during reset/reattach — retry opening
