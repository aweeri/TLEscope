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
echo "All done. Updated libraries:"
find lib/ -name "libraylib.a" -exec ls -lh {} \;
