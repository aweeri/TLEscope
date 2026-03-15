#!/bin/bash
set -e

# llvm-mingw appended to avoid shadowing coreutils
export PATH="$PATH:/opt/llvm-mingw/bin"

echo "Building raylib 5.5 for all platforms..."

./scripts/raylib-build.sh linux-x86_64
./scripts/raylib-build.sh linux-arm64
./scripts/raylib-build.sh windows-x86_64
./scripts/raylib-build.sh windows-arm64

echo "All done. Updated libraries:"
find lib/ -name "libraylib.a" -exec ls -lh {} \;
