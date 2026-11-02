#!/usr/bin/env bash
# demo_bench.sh [seconds] [log] — reset the master, let the attract loop play
# the demos on /blood, and print each demo's average fps (from the console's
# "demo <level>: ... fps" lines). Nothing else may use the console meanwhile.
# Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
source "$HERE/bench.conf"
secs="${1:-330}"; log="${2:-$(mktemp)}"
python3 "$HERE/uart.py" master capture --seconds "$secs" > "$log" 2>&1 &
cap=$!
sleep 1.5
timeout 20 openocd -f interface/cmsis-dap.cfg -f target/rp2350.cfg \
    -c "adapter serial $MASTER_PROBE" -c "adapter speed $MASTER_SWD_KHZ" \
    -c init -c "reset halt" -c resume -c exit >/dev/null 2>&1 || true
wait $cap || true
grep -a "demo .*fps\|Out of memory\|panic\|Error" "$log" | sed 's/^\[[ 0-9.]*\] //'
