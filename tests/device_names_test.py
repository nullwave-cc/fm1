#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 nullwave contributors
"""The names the FM-1 gives a computer: USB (app/fm1.toml: the manufacturer, the product that names the MIDI device
and its port, a serial number from the chip) and Bluetooth (app/app.h over fm1-base's bt/ble/ble_cfg.h), within their
limits, found by the installer's and the tools' port matching, and as the manual says. Run by tests/run_tests.sh.

    python3 tests/device_names_test.py
"""
import re
import sys
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
fails = []


def check(what, ok):
    print(("  ok " if ok else "FAIL ") + what)
    if not ok:
        fails.append(what)


def main():
    conf = tomllib.loads((ROOT / "app" / "fm1.toml").read_text(encoding="utf-8"))
    app_h = (ROOT / "app" / "app.h").read_text(encoding="utf-8")
    product, maker = conf.get("usb_product"), conf.get("usb_manufacturer")
    check('USB product "FM-1" (the MIDI device and its port)', product == "FM-1")
    check('USB manufacturer "nullwave"', maker == "nullwave")
    check("USB serial number on (a computer knows this FM-1 by it, not by its port)", conf.get("usb_serial") is True)
    check("the USB names in ASCII, the product 25 characters at most (felucca-bsp chip/usb.c)",
          all(s.isascii() for s in (product, maker)) and len(product) <= 25 and len(maker) <= 31)
    name = re.search(r'#define BLE_DEVICE_NAME "([^"]*)"', app_h)
    sep = re.search(r'#define BLE_NAME_SEP "([^"]*)"', app_h)
    check('Bluetooth name "nullwave FM-1" and a space before its four hex digits',
          bool(name and sep) and name.group(1) == "nullwave FM-1" and sep.group(1) == " ")
    ble = (name.group(1) + sep.group(1) + "A1B2") if name and sep else ""
    check("the Bluetooth name within fm1-base's 24 characters (before XXXX) and 29 in all",
          bool(name) and len(name.group(1) + sep.group(1)) <= 24 and len(ble) <= 29)
    ports = (ROOT / "base" / "web" / "fm1ports.js").read_text(encoding="utf-8")
    names = re.findall(r'"([^"]+)"', re.search(r"PORT_NAMES = \[([^\]]*)\]", ports).group(1))
    for n in (product, ble):
        check(f"the installer's port names find {n!r}", any(p in n.lower() for p in names))
    rx = re.search(r'PORT_RE = re\.compile\(r"([^"]+)"', (ROOT / "base" / "bsp" / "tools" / "fm1_install.py").read_text())
    check(f"fm1_install.py's PORT_RE finds {product!r}", bool(rx and re.search(rx.group(1), product, re.I)))
    man = (ROOT / "app" / "docs" / "MANUAL.md").read_text(encoding="utf-8")
    check("the manual names them", f"**{product}** by {maker}" in man and f"**{name.group(1) if name else ''}**" in man)
    print(f"device_names_test: {len(fails)} failed")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
