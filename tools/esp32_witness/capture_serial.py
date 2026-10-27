"""Capture the ESP32 witness serial output for a fixed time.

Usage: python capture_serial.py COM17 20 evidence/esp32-witness-run.log
"""
import sys
import time

import serial

port = sys.argv[1]
seconds = float(sys.argv[2])
out_path = sys.argv[3]

ser = serial.Serial()
ser.port = port
ser.baudrate = 115200
ser.timeout = 0.2
ser.dtr = False
ser.rts = False
ser.open()

start = time.time()
with open(out_path, "w", encoding="utf-8") as f:
    while time.time() - start < seconds:
        line = ser.readline()
        if line:
            f.write(line.decode("utf-8", "replace"))
            f.flush()
ser.close()
print(f"captured {seconds}s to {out_path}")
