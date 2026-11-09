#!/bin/sh
# Regenerate analysis/ with decomp-toolkit (tools/dtk; gcn-recomp/tools/fetch_dtk.sh fetches
# it). Needs a build first: it reads build/main.dol.
set -e
cd "$(dirname "$0")/../analysis"
../tools/dtk dol split config.yml out
find out -name '*.s' | sort | xargs cat > all.s
