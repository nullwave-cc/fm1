#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 nullwave contributors
"""Does the manual name everything the firmware shows? Every page title a button opens, every knob label on a page,
a layer or SETTINGS, and every sound parameter's label must appear in app/docs/MANUAL.md; prints what is missing.

    python3 tools/doc_check.py        (exit 1 if anything is missing)
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ui = (ROOT / "app" / "ui.c").read_text()
synth = (ROOT / "app" / "synth.c").read_text()
manual = (ROOT / "app" / "docs" / "MANUAL.md").read_text()

titles = re.search(r"PAGE_TITLE\[NPG\] = \{(.*?)\};", ui, re.S).group(1)
titles = re.findall(r'"([^"]*)"', titles)
pages = re.search(r"enum \{ PG_HOME,(.*?)NPG \};", ui, re.S).group(1)
pages = ["PG_HOME"] + [p.strip() for p in re.sub(r"/\*.*?\*/", "", pages, flags=re.S).split(",") if p.strip()]
groups = re.search(r"GROUP\[NGROUP\]\[GMAX\] = \{(.*?)\n\};", ui, re.S).group(1)
shown = set(re.findall(r"PG_\w+", groups))
menu = set(re.findall(r"PG_\w+", re.search(r"\[G_MENU\] = \{(.*?)\}", groups, re.S).group(1)))
shown -= menu                            # (SETTINGS shows its groups' names, not the pages' titles)
full = set(re.findall(r"case (KN_\w+): return \"", ui))   # (SETTINGS shows these knobs by their full names)
need = {}
for pg, t in zip(pages, titles):
    if pg in shown and t:
        need[t] = "page " + pg
labels = dict(re.findall(r'\[(KN_\w+)\] = "([^"]+)"', ui))
used = set()
for table in ("PAGE_KN", "LAYER_KN"):
    m = re.search(table + r"\[[^\]]*\]\[4\] = \{(.*?)\n\};", ui, re.S)
    used |= set(re.findall(r"KN_[A-Z0-9_]+", m.group(1)))
used |= set(re.findall(r"KN_[A-Z0-9_]+", re.search(r"STEP_KN\[4\] = \{(.*?)\};", ui).group(1)))
for kn in sorted(used):
    if kn in labels and kn not in full:
        need.setdefault(labels[kn], "knob " + kn)
for lab in re.findall(r'case KN_\w+: return "([^"]+)";', ui):   # SETTINGS' full names
    need.setdefault(lab, "setting")
for lab in re.findall(r'\{"([A-Z0-9 &+/]+)", F_[A-Z]+', synth):   # the sound's parameters
    need.setdefault(lab, "parameter")
missing = sorted(t for t in need if not re.search(r"(?<![A-Z0-9])" + re.escape(t) + r"(?![A-Z0-9])", manual))
for t in missing:
    print(f"not in the manual: {t!r} ({need[t]})")
print(f"doc_check: {len(need)} names, {len(missing)} missing")
sys.exit(1 if missing else 0)
