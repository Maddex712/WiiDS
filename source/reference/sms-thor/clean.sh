#!/usr/bin/env bash
# ./clean.sh [--all] [--dry-run]
#
# Deletes build output: every build/<os>-<arch>/ folder (with its
# sms-standalone or SMS.app), captures in build/, and the build folders of
# older layouts (build-mac/, build-64/, build32/, an old tree in build/).
# Never deletes a disc image: keeps rom/, and any folder holding an image.
# Keeps build/deps/ (the downloaded SDL2) and build/patchwork/
# (tools/mkpatch.sh edits). SMS_ARCH=32|64 deletes only that build.
#   --all      delete all of build/, including deps/ and patchwork/
#   --dry-run  list what would be deleted, delete nothing
set -euo pipefail

cd "$(dirname "$0")"
all=0
dry=0
for arg in "$@"; do
  case "$arg" in
    --all) all=1 ;;
    -n | --dry-run) dry=1 ;;
    -h | --help)
      sed -n '2,11p' "$0" | sed 's/^# \{0,1\}//'
      exit 0
      ;;
    *)
      echo "Unknown option $arg (see ./clean.sh --help)" >&2
      exit 2
      ;;
  esac
done
source tools/common.sh

# Rescue disc images from the rom/ folders of older layouts before deleting.
if (( dry )); then
  for d in "${sms_legacy_rom_dirs[@]}"; do
    for e in "${sms_image_exts[@]}"; do
      for f in "$d"/*."$e"; do
        if [[ -f "$f" && ! -e "rom/$(basename "$f")" ]]; then
          echo "Would move $f to rom/"
        fi
      done
    done
  done
else
  sms_migrate_legacy
fi

targets=()
if [[ -n "${SMS_ARCH:-}" ]]; then
  (( all == 0 )) || sms_die "--all deletes every build; leave out SMS_ARCH."
  sms_detect_os
  sms_select_arch build
  targets+=("$sms_build_dir")
else
  shopt -s dotglob nullglob
  if (( all )); then
    targets+=(build)
  else
    for f in build/*; do
      case "${f#build/}" in
        deps | patchwork) ;;
        *) targets+=("$f") ;;
      esac
    done
  fi
  targets+=("${sms_legacy_build_dirs[@]}")
  shopt -u dotglob nullglob
fi

found=()
for t in "${targets[@]}"; do
  [[ -e "$t" ]] || continue
  # Never delete a disc image. The only one the build makes is SMS.app's
  # packed disc.gcm; any other is the user's (in an old rom/ folder the move
  # above could not empty, or put there by hand).
  keep=0
  while IFS= read -r f; do
    if (( dry )) && [[ ! -e "rom/$(basename "$f")" ]]; then
      for d in "${sms_legacy_rom_dirs[@]}"; do
        [[ "$(dirname "$f")" == "$d" ]] && continue 2 # the real run moves it to rom/ first
      done
    fi
    keep=1
  done < <(find "$t" -type f \( -iname '*.iso' -o -iname '*.gcm' -o -iname '*.ciso' \) ! -path '*.app/Contents/Resources/disc.gcm' 2>/dev/null)
  if (( keep )); then
    echo "Keeping $t: it holds a disc image; move it to rom/ (or delete it yourself) first." >&2
    continue
  fi
  found+=("$t")
done
if (( ${#found[@]} == 0 )); then
  echo "Nothing to clean."
  exit 0
fi

if (( dry )); then
  echo "Would delete:"
else
  echo "Deleting:"
fi
for t in "${found[@]}"; do
  printf '  %6s  %s\n' "$(du -sh "$t" 2>/dev/null | cut -f1)" "$t"
done
(( dry )) && exit 0
rm -rf "${found[@]}"
echo "Done. rom/ is untouched; ./build.sh builds again."
