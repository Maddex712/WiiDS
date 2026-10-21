#!/usr/bin/env bash
# Builds a linker symbol-ordering file from a simpleperf capture of real gameplay, hottest first.
# Layout only: pass it to CMake as -DMKW_SYMBOL_ORDER_FILE=<out> and relink.
#
#   adb shell simpleperf record --app com.driftdroid.android --duration 30 -f 2000 -o /data/local/tmp/p.data
#   adb pull /data/local/tmp/p.data
#   runtime/tools/gen_symbol_order.sh p.data runtime/tools/symbol_order.txt
set -euo pipefail
perf_data=${1:?usage: gen_symbol_order.sh <perf.data> <out.txt>}
out=${2:?usage: gen_symbol_order.sh <perf.data> <out.txt>}
simpleperf=${SIMPLEPERF:-$HOME/android-sdk/ndk/27.2.12479018/simpleperf/bin/linux/x86_64/simpleperf}

"$simpleperf" report -i "$perf_data" --sort symbol --percent-limit 0 2>/dev/null |
    awk '/^[0-9]/ { pct=$1; sub(/%$/, "", pct); $1=""; sub(/^ +/, ""); if ($0 ~ /^[A-Za-z_][A-Za-z0-9_]*$/) print pct, $0 }' |
    sort -rnk1 |
    awk '{ $1=""; sub(/^ +/, ""); print }' > "$out"

echo "wrote $(wc -l < "$out") symbols to $out"
