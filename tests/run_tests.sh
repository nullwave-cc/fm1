#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Host tests: build the host simulator and run every script in app/tests.
set -e
cd "$(dirname "$0")/.."
./build.sh --host
for t in app/tests/basic.txt app/tests/companion.txt app/tests/engines.txt app/tests/mvp.txt app/tests/graphs.txt app/tests/midi.txt app/tests/seqviews.txt app/tests/layout.txt app/tests/fm6.txt app/tests/buttons.txt app/tests/idle.txt app/tests/glo.txt app/tests/chords.txt app/tests/recmodes.txt app/tests/legends.txt app/tests/logo.txt app/tests/names.txt app/tests/replace.txt app/tests/views.txt app/tests/keys.txt app/tests/modulation.txt app/tests/usernames.txt app/tests/silence.txt app/tests/settings_reset.txt app/tests/factory_reset.txt app/tests/stress.txt app/tests/battery.txt; do
    echo "== $t"
    app/build/host/nullwave_host "$t" app/build/host
done
echo "== app/tests/installed.txt (a new build's first start)"
NW_FRESH=1 app/build/host/nullwave_host app/tests/installed.txt app/build/host
echo "== the editor's way in (the companion: parameters, the preset file)"
python3 tests/editor_test.py
echo "== the factory reset over the companion (the installer's way)"
python3 tests/factory_test.py
echo "== the chord names (app/chords.c)"
cc -o app/build/host/chord_test tests/chord_test.c
app/build/host/chord_test
echo "== the manual against the firmware"
python3 tools/doc_check.py
