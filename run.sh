#!/usr/bin/env bash
# Convenience launcher: builds the C editor if needed, then runs it.
set -euo pipefail
cd "$(dirname "$0")"

if [ ! -x build/anvil ]; then
    make -j"${JOBS:-4}"
fi

exec ./build/anvil "$@"
