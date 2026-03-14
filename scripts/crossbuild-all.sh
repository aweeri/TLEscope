#!/bin/bash
set -e

# llvm-mingw appended to avoid shadowing coreutils
export PATH="$PATH:/opt/llvm-mingw/bin"

echo "Building raylib 5.5 for all platforms..."

./scripts/build-raylib.sh linux-x86_64
./scripts/build-raylib.sh linux-arm64
./scripts/build-raylib.sh windows-x86_64
./scripts/build-raylib.sh windows-arm64

echo ""
echo "Test-linking Linux ARM64 app..."
make linux \
    CC_LINUX=aarch64-linux-gnu-gcc \
    LIB_LIN_PATH="-Ilib/raylib_lin_arm64/include -Llib/raylib_lin_arm64/lib" \
    LDFLAGS_LIN="-Ilib/raylib_lin_arm64/include -Llib/raylib_lin_arm64/lib -lraylib -lcurl -lGL -lm -lpthread -ldl -lrt -lX11" \
    DIST_LINUX=dist/TLEscope-Linux-arm64-Portable

echo ""
echo "All done. Updated libraries:"
find lib/ -name "libraylib.a" -exec ls -lh {} \;
