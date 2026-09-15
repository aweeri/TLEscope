#!/bin/sh
# gen_versioninfo.sh - render src/versioninfo.rc.in into a Windows .rc file.
#
# Usage: gen_versioninfo.sh <version_num> <version_str> <outfile>
#   version_num:  MAJOR.MINOR.PATCH.COMMITS (e.g. 3.9.2.12)
#   version_str:  full human version (e.g. v3.9.2-12-g4f2a1c9-dirty)

set -u

VERSION_NUM="${1:?missing version_num}"
VERSION_STR="${2:?missing version_str}"
OUT="${3:?missing outfile}"

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
TEMPLATE="$SCRIPT_DIR/../src/versioninfo.rc.in"

VERSION_COMMA=$(printf '%s\n' "$VERSION_NUM" | tr '.' ',')

sed -e "s/@FILEVERSION_COMMA@/$VERSION_COMMA/g" \
    -e "s/@PRODUCTVERSION_COMMA@/$VERSION_COMMA/g" \
    -e "s/@FILEVERSION_STR@/$VERSION_NUM/g" \
    -e "s/@PRODUCTVERSION_STR@/$VERSION_STR/g" \
    "$TEMPLATE" > "$OUT"