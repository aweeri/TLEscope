#!/usr/bin/env bash
# bundles a freshly built macOS binary into a proper TLEscope.app
#
# does everything natively on the build machine:
#   - compiles AppIcon.icns from logo.png (sips + iconutil, no precompiled icon)
#   - generates Info.plist with the real git version baked in
#   - ad-hoc codesigns by default (arm64 binaries refuse to run unsigned)
#
# usage: scripts/macos-bundle.sh [binary]     default binary: bin/TLEscope-macos
#
# env vars:
#   CODE_SIGN_IDENTITY   set to e.g. "Developer ID Application: Your Name (TEAMID)"
#                        for real signing. default is "-" (ad-hoc).
set -euo pipefail

APP_NAME="TLEscope"
BUNDLE_ID="com.aweeri.tlescope"
BIN="${1:-bin/${APP_NAME}-macos}"
APP="dist/${APP_NAME}.app"

GIT_VERSION=$(git describe --tags --always --dirty 2>/dev/null || echo "vUnknown")

for tool in sips iconutil codesign; do
    command -v "$tool" >/dev/null || { echo "error: '$tool' not found - this script must run on macOS"; exit 1; }
done
[ -x "$BIN" ] || { echo "error: $BIN not found - run 'make macos' first"; exit 1; }

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"

# portable resource layout lives inside Resources (the binary chdirs there)
cp -r themes "$APP/Contents/Resources/themes"
cp logo.png logo_l.png "$APP/Contents/Resources/"
cp settings.json "$APP/Contents/Resources/" 2>/dev/null || true
cp data.tle "$APP/Contents/Resources/" 2>/dev/null || true
cp "$BIN" "$APP/Contents/MacOS/$APP_NAME"

# compile a multi-resolution .icns from logo.png
ICONSET_DIR="$(mktemp -d)"
ICONSET="$ICONSET_DIR/${APP_NAME}.iconset"
mkdir -p "$ICONSET"
for size in 16 32 128 256 512; do
    double=$((size * 2))
    sips -z "$size" "$size" logo.png --out "$ICONSET/icon_${size}x${size}.png" >/dev/null
    sips -z "$double" "$double" logo.png --out "$ICONSET/icon_${size}x${size}@2x.png" >/dev/null
done
iconutil -c icns "$ICONSET" -o "$APP/Contents/Resources/AppIcon.icns"
rm -rf "$ICONSET_DIR"

cat > "$APP/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key>
    <string>${APP_NAME}</string>
    <key>CFBundleIconFile</key>
    <string>AppIcon</string>
    <key>CFBundleIdentifier</key>
    <string>${BUNDLE_ID}</string>
    <key>CFBundleName</key>
    <string>${APP_NAME}</string>
    <key>CFBundleDisplayName</key>
    <string>${APP_NAME}</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>${GIT_VERSION}</string>
    <key>CFBundleVersion</key>
    <string>${GIT_VERSION}</string>
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>LSMinimumSystemVersion</key>
    <string>11.0</string>
    <key>NSHumanReadableCopyright</key>
    <string>See LICENSE</string>
</dict>
</plist>
EOF

# signing: ad-hoc by default so the app actually launches on Apple Silicon.
# with CODE_SIGN_IDENTITY set we also opt into hardened runtime (needed for notarization).
# note: settings.json lives inside Resources, so editing it in-place after
# signing invalidates the signature - Gatekeeper only cares on first launch tho
if [ "${CODE_SIGN_IDENTITY:--}" = "-" ]; then
    echo "ad-hoc signing $APP (set CODE_SIGN_IDENTITY for real signing)"
    codesign --force --deep --sign - "$APP"
else
    codesign --force --deep --sign "$CODE_SIGN_IDENTITY" --options runtime "$APP"
fi
codesign --verify --strict "$APP"

echo "built $APP (${GIT_VERSION})"
