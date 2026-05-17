#!/usr/bin/env python3
"""Print likely USB serial ports, one per line."""

import re

from serial.tools import list_ports


COM_PORT_RE = re.compile(r"^COM\d+$", re.IGNORECASE)

KEYWORDS = (
    "usb",
    "uart",
    "cp210",
    "ch340",
    "ch910",
    "ftdi",
    "silicon",
    "espressif",
    "jtag",
    "serial",
)


def likely_usb_serial(port: object) -> bool:
    device = getattr(port, "device", "") or ""
    description = getattr(port, "description", "") or ""
    hwid = getattr(port, "hwid", "") or ""
    text = f"{device} {description} {hwid}".lower()

    if "bluetooth" in text:
        return False

    vid = getattr(port, "vid", None)
    pid = getattr(port, "pid", None)
    if vid is not None and pid is not None:
        return True

    if COM_PORT_RE.match(device):
        return True

    return any(keyword in text for keyword in KEYWORDS)


def main() -> None:
    seen = set()
    for port in list_ports.comports():
        device = port.device
        if device in seen or not likely_usb_serial(port):
            continue
        print(device)
        seen.add(device)


if __name__ == "__main__":
    main()
