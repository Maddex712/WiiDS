#!/bin/sh
# mkpatch.sh NAME REASON FILE... : diff edited copies in build/patchwork/b against decomp originals.
# Workflow: tools/mkpatch.sh --edit FILE  copies decomp/FILE to build/patchwork/b/FILE for editing.
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
if [ "$1" = "--edit" ]; then
  shift
  for f in "$@"; do mkdir -p "$root/build/patchwork/b/$(dirname "$f")"; cp "$root/decomp/$f" "$root/build/patchwork/b/$f"; done
  exit 0
fi
name=$1; reason=$2; shift 2
out="$root/decomp-patches/$name.patch"
printf '%s\n' "Reason: $reason" > "$out"
for f in "$@"; do
  (cd "$root" && diff -u --label "a/$f" --label "b/$f" "decomp/$f" "build/patchwork/b/$f" >> "$out") || true
done
echo "wrote $out"
