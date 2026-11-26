#!/usr/bin/env bash
# Install the C editor + Python visualizer and expose `anvil` / `anvil-viz`.
set -euo pipefail
cd "$(dirname "$0")"

if ! command -v cc >/dev/null 2>&1; then
    echo "A C compiler (cc) is required to build anvil." >&2
    exit 1
fi
make -j"${JOBS:-4}"

if [ ! -x .venv/bin/python ]; then
    python3 -m venv .venv
fi

log="$(mktemp)"
if ! .venv/bin/pip install --disable-pip-version-check -q -e . >"$log" 2>&1; then
    cat "$log" >&2
    rm -f "$log"
    echo "Install failed." >&2
    exit 1
fi
rm -f "$log"

target="${XDG_BIN_HOME:-$HOME/.local/bin}"
mkdir -p "$target"
ln -sfn "$PWD/build/anvil" "$target/anvil"
ln -sfn "$PWD/.venv/bin/anvil-viz" "$target/anvil-viz"

case ":$PATH:" in
    *":$target:"*) ;;
    *) echo "note: add $target to your PATH to run 'anvil' and 'anvil-viz' from anywhere" ;;
esac

echo "Installed: $target/anvil      (C editor)"
echo "Installed: $target/anvil-viz  (Python visualizer)"
echo "Run the editor:     anvil [file]"
echo "Run the visualizer: anvil-viz"
