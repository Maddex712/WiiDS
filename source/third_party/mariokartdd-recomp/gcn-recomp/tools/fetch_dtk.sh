#!/bin/sh
# Downloads decomp-toolkit (dtk), used to (re)generate analysis/symbols.txt.
cd "$(dirname "$0")"
case "$(uname -s)-$(uname -m)" in
  Darwin-arm64) asset=dtk-macos-arm64 ;;
  Darwin-x86_64) asset=dtk-macos-x86_64 ;;
  Linux-x86_64) asset=dtk-linux-x86_64 ;;
  MINGW*-x86_64|MSYS*-x86_64) asset=dtk-windows-x86_64.exe; out=dtk.exe ;;
  *) echo "unsupported host; see https://github.com/encounter/decomp-toolkit/releases"; exit 1 ;;
esac
out=${out:-dtk}
curl -sL -o "$out" "https://github.com/encounter/decomp-toolkit/releases/latest/download/$asset" && chmod +x "$out"
