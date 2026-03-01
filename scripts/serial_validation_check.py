#!/usr/bin/env python3
"""Watch serial output and assert validation markers are present."""

import argparse
import sys
import time

import serial


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--seconds", type=int, default=30)
    args = parser.parse_args()

    deadline = time.time() + args.seconds
    saw_tel = False
    saw_nvs_pass = False

    print(f"Opening serial {args.port} @ {args.baud} for {args.seconds}s")
    with serial.Serial(args.port, args.baud, timeout=0.2) as ser:
        while time.time() < deadline:
            raw = ser.readline()
            if not raw:
                continue

            line = raw.decode("utf-8", errors="replace").strip()
            if not line:
                continue

            print(line)

            if "TEL:{" in line:
                saw_tel = True
            if "NVS_SELFTEST:PASS" in line:
                saw_nvs_pass = True

    if not saw_nvs_pass:
        print("ERROR: missing NVS_SELFTEST:PASS marker", file=sys.stderr)
        return 1
    if not saw_tel:
        print("ERROR: missing telemetry TEL:{...} marker", file=sys.stderr)
        return 1

    print("Serial validation markers OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
