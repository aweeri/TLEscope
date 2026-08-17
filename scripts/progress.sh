#!/bin/bash
# scripts/progress.sh - fancy colorful progress bar for TLEscope builds :3
#
# Wraps compiler commands to show a live progress bar during compilation.
# Usage: progress.sh <compiler_command...>

COUNTER_FILE="${COUNTER_FILE:-/tmp/tlescope_build_counter}"
TOTAL_FILE="${TOTAL_FILE:-/tmp/tlescope_build_total}"

# If total file doesn't exist, just run the command directly (fallback)
if [ ! -f "$TOTAL_FILE" ]; then
    exec "$@"
fi

TOTAL=$(cat "$TOTAL_FILE" 2>/dev/null || echo 1)
if [ "$TOTAL" -eq 0 ]; then TOTAL=1; fi

# increment the counter (flock so it doesnt crap out on parallel building)
if command -v flock &>/dev/null; then
    (
        flock -x 200
        c=$(($(cat "$COUNTER_FILE" 2>/dev/null || echo 0) + 1))
        echo "$c" > "$COUNTER_FILE"
    ) 200>"${COUNTER_FILE}.lock"
    # Re-read after the subshell
    COUNT=$(cat "$COUNTER_FILE" 2>/dev/null || echo 1)
else
    COUNT=$(($(cat "$COUNTER_FILE" 2>/dev/null || echo 0) + 1))
    echo "$COUNT" > "$COUNTER_FILE"
fi

# Calculate percentage (cap at 100)
PERCENT=$((COUNT * 100 / TOTAL))
[ "$PERCENT" -gt 100 ] && PERCENT=100

# -- ANSI escape codes --
RST='\033[0m'
BOLD='\033[1m'

# -- Pick color by progress (gay rights wooo) --
if   [ "$PERCENT" -lt 15 ]; then
    C='\033[0;31m'   # red
elif [ "$PERCENT" -lt 30 ]; then
    C='\033[0;33m'   # orange
elif [ "$PERCENT" -lt 50 ]; then
    C='\033[1;33m'   # yellow
elif [ "$PERCENT" -lt 65 ]; then
    C='\033[0;32m'   # green
elif [ "$PERCENT" -lt 80 ]; then
    C='\033[0;36m'   # cyan
elif [ "$PERCENT" -lt 95 ]; then
    C='\033[0;34m'   # blue
else
    C='\033[0;35m'   # purple
fi

# Draw
W=22
F=$((PERCENT * W / 100))
E=$((W - F))
BAR=""
for ((i=0; i<F; i++)); do BAR="${BAR}█"; done
for ((i=0; i<E; i++)); do BAR="${BAR}░"; done

# -- Extract the source filename being compiled --
SRC=""
for arg in "$@"; do
    if [[ "$arg" == *.cpp || "$arg" == *.c ]]; then
        SRC=$(basename "$arg")
        break
    fi
done

# -- Print sum colors~~ --
echo -e "${C}${BAR}${RST} ${BOLD}${COUNT}/${TOTAL}${RST} ${C}(${PERCENT}%)${RST} ${SRC}"

# -- Run the actual compiler lol --
"$@"