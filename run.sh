#!/usr/bin/env bash
# Convenience launcher: creates the local venv on first run, then starts the editor.
set -euo pipefail
cd "$(dirname "$0")"

if [ ! -x .venv/bin/python ]; then
    python3 -m venv .venv
    .venv/bin/pip install --disable-pip-version-check -q -r requirements.txt
fi

exec .venv/bin/python -m dsa_editor "$@"
