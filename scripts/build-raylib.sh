#!/bin/bash
set -e

RAYLIB_VERSION="5.5"
RAYLIB_REPO="https://github.com/raysan5/raylib.git"
BUILD_DIR="/tmp/raylib-build-$$"
PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PATCH_DIR="$PROJECT_ROOT/patches"

detect_platform() {
    local os arch
    os="$(uname -s)"
    arch="$(uname -m)"
    case "$os" in
        Linux)
            case "$arch" in
                x86_64)  echo "linux-x86_64" ;;
                aarch64) echo "linux-arm64" ;;
                *)       echo "linux-$arch" ;;
            esac ;;
        Darwin) echo "macos" ;;
        MINGW*|MSYS*|CYGWIN*)
            # MSYS2: use $MSYSTEM to detect arch (uname -m lies on ARM64)
            case "$MSYSTEM" in
                CLANGARM64)  echo "windows-arm64" ;;
                *)           echo "windows-x86_64" ;;
            esac ;;
        *) echo "unknown"; exit 1 ;;
    esac
}

PLATFORM="${1:-$(detect_platform)}"

echo "Building raylib $RAYLIB_VERSION for $PLATFORM..."

if [ -d "$BUILD_DIR" ]; then rm -rf "$BUILD_DIR"; fi
git clone --depth 1 --branch "$RAYLIB_VERSION" "$RAYLIB_REPO" "$BUILD_DIR" 2>&1 | tail -1

# Patches live in patches/all/, patches/linux/, patches/windows/, patches/macos/
apply_patches() {
    local dir="$1"
    [ -d "$dir" ] || return 0
    for p in "$dir"/*.patch; do
        [ -f "$p" ] || continue
        echo "  Patching $(basename "$p")..."
        (cd "$BUILD_DIR" && git apply "$p")
    done
}

apply_patches "$PATCH_DIR/all"
case "$PLATFORM" in
    linux-*)   apply_patches "$PATCH_DIR/linux" ;;
    windows-*) apply_patches "$PATCH_DIR/windows" ;;
    macos-*)   apply_patches "$PATCH_DIR/macos" ;;
esac

NPROC="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"
cd "$BUILD_DIR/src"
case "$PLATFORM" in
    linux-x86_64)
        make PLATFORM=PLATFORM_DESKTOP GLFW_LINUX_ENABLE_WAYLAND=TRUE -j"$NPROC"
        DEST="$PROJECT_ROOT/lib/raylib_lin"
        ;;
    linux-arm64)
        make PLATFORM=PLATFORM_DESKTOP GLFW_LINUX_ENABLE_WAYLAND=TRUE \
            CC=aarch64-linux-gnu-gcc AR=aarch64-linux-gnu-ar -j"$NPROC"
        DEST="$PROJECT_ROOT/lib/raylib_lin_arm64"
        ;;
    windows-x86_64)
        if [ -n "$MSYSTEM" ]; then
            make PLATFORM=PLATFORM_DESKTOP -j"$NPROC"
        else
            make PLATFORM=PLATFORM_DESKTOP CC=x86_64-w64-mingw32-gcc AR=x86_64-w64-mingw32-ar \
                OS=Windows_NT -j"$NPROC"
        fi
        DEST="$PROJECT_ROOT/lib/raylib_win"
        ;;
    windows-arm64)
        if [ -n "$MSYSTEM" ]; then
            make PLATFORM=PLATFORM_DESKTOP CC=cc AR=ar -j"$NPROC"
        else
            make PLATFORM=PLATFORM_DESKTOP CC=aarch64-w64-mingw32-gcc AR=aarch64-w64-mingw32-ar \
                OS=Windows_NT -j"$NPROC"
        fi
        DEST="$PROJECT_ROOT/lib/raylib_win_arm64"
        ;;
    macos*)
        make PLATFORM=PLATFORM_DESKTOP -j"$NPROC"
        DEST="$PROJECT_ROOT/lib/raylib_macos"
        ;;
    *)
        echo "Unknown platform: $PLATFORM"
        echo "Supported: linux-x86_64, linux-arm64, windows-x86_64, windows-arm64, macos"
        exit 1
        ;;
esac

mkdir -p "$DEST/lib" "$DEST/include"
cp "$BUILD_DIR/src/libraylib.a" "$DEST/lib/"
cp "$BUILD_DIR/src/raylib.h" "$DEST/include/"
cp "$BUILD_DIR/src/raymath.h" "$DEST/include/"
cp "$BUILD_DIR/src/rlgl.h" "$DEST/include/"

echo "Done, installed to $DEST"
rm -rf "$BUILD_DIR"
