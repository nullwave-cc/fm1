#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Host tests: build the host simulator and run every script in app/tests.
set -e
cd "$(dirname "$0")/.."
./build.sh --host
for t in app/tests/basic.txt app/tests/companion.txt app/tests/engines.txt app/tests/mvp.txt app/tests/graphs.txt app/tests/midi.txt app/tests/seqviews.txt app/tests/layout.txt app/tests/fm6.txt app/tests/buttons.txt app/tests/idle.txt app/tests/glo.txt app/tests/chords.txt app/tests/recmodes.txt; do
    echo "== $t"
    app/build/host/nullwave_host "$t" app/build/host
done
echo "== the manual against the firmware"
python3 tools/doc_check.py
