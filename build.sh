#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Build nullwave with fm1-base (base/, a git submodule).
#   ./build.sh [--release X.Y] [--no-ble] [--host | --web]
#   ./build.sh --setup             fetch the pinned JieLi toolchain and SDK files into base/.toolchain (once)
#   JIELI_TOOLCHAIN  JieLi Linux toolchain (default: base/.toolchain, then ~/.jieli/toolchain)
#   AC79_SDK         JieLi AC79 SDK checkout (default: base/.toolchain/ac79-sdk, then ~/fw-AC79_AIoT_SDK)
#   PYTHON           a Python with Pillow
# On macOS the JieLi tools run in a container: Docker or podman must be installed and running.
set -e
cd "$(dirname "$0")"
if [ "${1:-}" = --setup ]; then
    exec base/tools/setup_toolchain.sh --only jieli,ac79-sdk
fi
PY="${PYTHON:-python3}"
# macOS: let a python.org Python find Homebrew's FriBiDi (Pillow's raqm, for the fonts)
if [ "$(uname)" = Darwin ] && [ -d /opt/homebrew/lib ]; then
    export DYLD_FALLBACK_LIBRARY_PATH="${DYLD_FALLBACK_LIBRARY_PATH:-$HOME/lib:/usr/local/lib:/usr/lib}:/opt/homebrew/lib"
fi
[ -f base/tools/build.py ] || { echo "build.sh: no fm1-base in base/ (git submodule update --init --recursive)"; exit 1; }
"$PY" -c 'import PIL' 2>/dev/null || { echo "build.sh: $PY has no Pillow (pip3 install Pillow)"; exit 1; }
exec "$PY" base/tools/build.py app "$@"
