#!/usr/bin/env bash
# tools/gx/record_shader_keys.sh OUT BUILD_DIR STAGE[,SCENARIO]...
#
# Records the shader program keys each listed stage draws with into OUT (for
# tools/gx/shader_keys.py): the game runs headless, loads a new file, warps
# there (SMS_WARP) and walks about. Needs a disc image (SMS_DISC_IMAGE, or
# the one in rom/). SECS=n sets each run's length (default 75), JOBS=n how
# many run at once (default 2).
#
#   tools/gx/record_shader_keys.sh keys.txt build/linux-64 1,0 2,0 2,1 3,0
#   python3 tools/gx/shader_keys.py keys.txt
set -uo pipefail
out=$(realpath -m "$1")
build=$(realpath "$2")
shift 2
cd "$(dirname "$0")/../.."
disc=${SMS_DISC_IMAGE:-$(ls rom/*.iso rom/*.gcm rom/*.ciso 2>/dev/null | head -1)}
[[ -n "$disc" ]] || { echo "no disc image: set SMS_DISC_IMAGE" >&2; exit 1; }
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
# Through the title and file select into the warp, then a walk with the
# camera turning: 120 fps fields, as SMS_AUTOPRESS counts them.
press='START@1400,STICK_LEFT@2000,A@2400,STICK_LEFT@3100,A@3250,A@3450,A@3800'
press+=',STICK_UP@4400+600,CSTICK_RIGHT@4600+300,B@4800,STICK_RIGHT@5200+400,CSTICK_LEFT@5400+300'
press+=',STICK_DOWN@5600+600,A@5900,STICK_UP@6400+800,CSTICK_RIGHT@6800+400,STICK_LEFT@7400+600'
run() {
  local warp=$1 dir="$tmp/${1//,/_}"
  mkdir -p "$dir/save"
  { env -i HOME="$HOME" PATH="$PATH" DISPLAY="${DISPLAY:-}" SMS_SETTINGS="$dir/none.txt" \
    SMS_HEADLESS=1 SMS_AUDIO=0 SMS_SKIP_MOVIES=1 SMS_VI_DETERMINISTIC=1 SMS_QUIET_STUBS=1 \
    SMS_SAVE_DIR="$dir/save" SMS_FRAME_RATE=120 SMS_TEXTURE_PACKS=0 SMS_GX_SHADER_CACHE=0 SMS_GX_SHADER_WARMUP=0 \
    SMS_WARP="$warp" SMS_AUTOPRESS="$press" SMS_DISC_IMAGE="$disc" SMS_GX_SHADER_KEYS="$dir/keys.txt" \
    timeout -s KILL "${SECS:-75}" "$build/sms" > "$dir/log.txt" 2>&1; } 2>/dev/null
  local n; n=$(wc -l < "$dir/keys.txt" 2>/dev/null || echo 0)
  echo "$warp: $n keys$(grep -q 'SMS_WARP: stage' "$dir/log.txt" || echo ' (no warp)')"
}
for w in "$@"; do
  while (( $(jobs -rp | wc -l) >= ${JOBS:-2} )); do wait -n; done
  run "$w" &
done
wait
cat "$tmp"/*/keys.txt >> "$out" 2>/dev/null
echo "$(wc -l < "$out") lines in $out"
