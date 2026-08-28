#!/usr/bin/env python3
"""Print the device log with timestamps.

`idf.py monitor` needs the esp_idf_monitor package, which the PlatformIO
Python environment does not have. This reads the same lines over the same
serial port. What it does not do is decode backtrace addresses into function
names; for that, install ESP-IDF the normal way and use its monitor.

    python tools/monitor.py /dev/ttyACM0            # follow until Ctrl-C
    python tools/monitor.py /dev/ttyACM0 30         # stop after 30 seconds
    python tools/monitor.py /dev/ttyACM0 30 reset   # reset the board first
"""

import sys
import time

import serial

USAGE = "usage: monitor.py PORT [SECONDS] [reset]"


def main():
    if len(sys.argv) < 2:
        sys.exit(USAGE)

    port = sys.argv[1]
    seconds = float(sys.argv[2]) if len(sys.argv) > 2 else None
    reset = "reset" in sys.argv[3:]

    link = serial.Serial(port, 115200, timeout=0.2)

    if reset:
        #  Pull EN low over RTS, let go, and the board starts fresh.
        link.setDTR(False)
        link.setRTS(True)
        time.sleep(0.15)
        link.setRTS(False)
        link.reset_input_buffer()

    started = time.time()
    pending = b""

    try:
        while seconds is None or time.time() - started < seconds:
            chunk = link.read(4096)
            if not chunk:
                continue
            pending += chunk
            while b"\n" in pending:
                line, pending = pending.split(b"\n", 1)
                stamp = time.time() - started
                text = line.decode("utf-8", "replace").rstrip("\r")
                print(f"[{stamp:7.2f}] {text}", flush=True)
    except KeyboardInterrupt:
        pass
    finally:
        link.close()


if __name__ == "__main__":
    main()
