#!/usr/bin/env python3
"""Simple on-device HTTP validation for telemetry and params APIs."""

import argparse
import json
import sys
import urllib.parse
import urllib.request


def http_get(url: str) -> str:
    with urllib.request.urlopen(url, timeout=5) as resp:
        return resp.read().decode("utf-8", errors="replace")


def http_post(url: str) -> str:
    req = urllib.request.Request(url, method="POST")
    with urllib.request.urlopen(req, timeout=5) as resp:
        return resp.read().decode("utf-8", errors="replace")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True, help="ESP32 host/IP")
    args = parser.parse_args()

    base = f"http://{args.host}"

    print("[1] GET /api/telemetry")
    telem_raw = http_get(f"{base}/api/telemetry")
    telem = json.loads(telem_raw)
    required_keys = [
        "timestamp_us",
        "loop_count",
        "active_nodes",
        "degradation_mode",
    ]
    missing = [k for k in required_keys if k not in telem]
    if missing:
        print(f"Missing telemetry keys: {missing}")
        return 1
    print("Telemetry payload OK")

    print("[2] GET /api/params")
    params_raw = http_get(f"{base}/api/params")
    params = json.loads(params_raw)
    if not isinstance(params, dict):
        print("/api/params response is not JSON object")
        return 1
    print(f"Parameter count (JSON keys): {len(params)}")

    print("[3] POST /api/params set drive.max_speed=0.57")
    q = urllib.parse.urlencode({"name": "drive.max_speed", "value": "0.57"})
    post_resp = http_post(f"{base}/api/params?{q}")
    if "\"ok\":true" not in post_resp:
        print(f"POST failed: {post_resp}")
        return 1

    print("[4] Verify updated value")
    params_raw = http_get(f"{base}/api/params")
    params = json.loads(params_raw)
    value = params.get("drive.max_speed")
    if value is None:
        print("drive.max_speed not found after update")
        return 1

    try:
        val = float(value)
    except Exception:
        print(f"drive.max_speed not numeric: {value}")
        return 1

    if abs(val - 0.57) > 0.01:
        print(f"drive.max_speed unexpected value: {val}")
        return 1

    print("HTTP telemetry/params validation passed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"Validation failed with exception: {exc}", file=sys.stderr)
        raise
