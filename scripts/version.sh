#!/bin/sh
# version.sh - derive TLEscope version strings from git.
#
# Usage:
#   version.sh describe   -> full git describe string (e.g. v3.9.2-12-g4f2a1c9-dirty)
#   version.sh num        -> numeric MAJOR.MINOR.PATCH.COMMITS (e.g. 3.9.2.12)
#   version.sh num-comma  -> numeric comma form for .rc files (e.g. 3,9,2,12)
#
# The numeric form is used for Windows VERSIONINFO resources and the NSIS
# installer, both of which require a strict x.y.z.w numeric version.

set -u

DESC=$(git describe --tags --always --dirty 2>/dev/null) || DESC="vUnknown"

describe() {
    printf '%s\n' "$DESC"
}

num() {
    # strip the leading 'v' (git tag convention)
    body=${DESC#v}

    # base = everything before the first '-' (the tag), rest = everything after
    base=${body%%-*}
    rest=${body#*-}
    if [ "$rest" = "$body" ]; then
        rest=""
    fi

    # commit count: leading digits of rest (empty / dirty / bare hash -> 0)
    commits=$(printf '%s\n' "$rest" | sed -n 's/^\([0-9][0-9]*\).*/\1/p')
    [ -n "$commits" ] || commits=0

    # split the tag into major.minor.patch (non-numeric tags -> 0.0.0)
    major=$(printf '%s\n' "$base" | sed -n 's/^\([0-9][0-9]*\)\..*/\1/p')
    minor=$(printf '%s\n' "$base" | sed -n 's/^[0-9][0-9]*\.\([0-9][0-9]*\).*/\1/p')
    patch=$(printf '%s\n' "$base" | sed -n 's/^[0-9][0-9]*\.[0-9][0-9]*\.\([0-9][0-9]*\).*/\1/p')
    [ -n "$major" ] || major=0
    [ -n "$minor" ] || minor=0
    [ -n "$patch" ] || patch=0

    printf '%s.%s.%s.%s\n' "$major" "$minor" "$patch" "$commits"
}

case "${1:-}" in
    describe)  describe ;;
    num)       num ;;
    num-comma) num | tr '.' ',' ;;
    *)
        echo "usage: $0 {describe|num|num-comma}" >&2
        exit 1
        ;;
esac