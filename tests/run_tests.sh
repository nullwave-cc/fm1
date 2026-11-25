#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Host tests: build the host simulator and run every script in app/tests.
set -e
cd "$(dirname "$0")/.."
./build.sh --host
for t in app/tests/basic.txt app/tests/companion.txt; do
    echo "== $t"
    app/build/host/nullwave_host "$t" app/build/host
done
