#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT_DIR"

PORTABLE_DIR="${1:-dist/TLEscope-macOS-Portable}"
APP_DIR="${2:-dist/TLEscope.app}"

if [[ ! -x "$PORTABLE_DIR/TLEscope" ]]; then
    echo "Missing macOS executable: $PORTABLE_DIR/TLEscope" >&2
    exit 1
fi

if [[ ! -d "$PORTABLE_DIR/themes" ]]; then
    echo "Missing theme resources: $PORTABLE_DIR/themes" >&2
    exit 1
fi

rm -rf "$APP_DIR"
cp -R bundle "$APP_DIR"
mkdir -p "$APP_DIR/Contents/Resources"

cp -R "$PORTABLE_DIR"/. "$APP_DIR/Contents/Resources/"
mv "$APP_DIR/Contents/Resources/TLEscope" "$APP_DIR/Contents/MacOS/TLEscope.bin"

chmod +x "$APP_DIR/Contents/MacOS/TLEscope"
chmod +x "$APP_DIR/Contents/MacOS/TLEscope.bin"

echo "macOS app bundle created at $APP_DIR"
