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
    parser.add_argument(
        "--telemetry-mode",
        choices=("full", "compact", "either"),
        default="full",
        help="Expected serial telemetry format. full=TEL:{...}, compact=TEL:t=..., either=accept both",
    )
    parser.add_argument(
        "--require-nvs-pass",
        action="store_true",
        help="Require NVS_SELFTEST:PASS marker",
    )
    args = parser.parse_args()

    deadline = time.time() + args.seconds
    saw_tel_full = False
    saw_tel_compact = False
    saw_nvs_pass = False

    print(
        f"Opening serial {args.port} @ {args.baud} for {args.seconds}s "
        f"(telemetry_mode={args.telemetry_mode}, require_nvs_pass={args.require_nvs_pass})"
    )
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
                saw_tel_full = True
            if line.startswith("TEL:t="):
                saw_tel_compact = True
            if "NVS_SELFTEST:PASS" in line:
                saw_nvs_pass = True

    if args.require_nvs_pass and not saw_nvs_pass:
        print("ERROR: missing NVS_SELFTEST:PASS marker", file=sys.stderr)
        return 1
    if args.telemetry_mode == "full" and not saw_tel_full:
        print("ERROR: missing full telemetry marker TEL:{...}", file=sys.stderr)
        return 1
    if args.telemetry_mode == "compact" and not saw_tel_compact:
        print("ERROR: missing compact telemetry marker TEL:t=...", file=sys.stderr)
        return 1
    if args.telemetry_mode == "either" and not (saw_tel_full or saw_tel_compact):
        print("ERROR: missing telemetry marker (full or compact)", file=sys.stderr)
        return 1

    print(
        "Serial validation markers OK "
        f"(full={int(saw_tel_full)} compact={int(saw_tel_compact)} nvs={int(saw_nvs_pass)})"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
