#!/usr/bin/env bash
# tools/make_mac_app.sh APP EXE SDL2_FRAMEWORK
#
# Fills in the app bundle APP (e.g. build/macos-64/SMS.app) around the files
# already in APP/Contents/Resources: disc.gcm (tools/bundle_disc.py
# --image-only) and SMS.icns (tools/extract_icon.py).
#   Contents/MacOS/sms                  EXE (finds SDL2 via @executable_path/../Frameworks)
#   Contents/Frameworks/SDL2.framework  SDL2_FRAMEWORK
#   Contents/Info.plist
# then signs the bundle ad hoc. platform/disc reads the image from
# Contents/Resources, so the app needs no disc image and no SDL2 install.
set -euo pipefail

if (( $# != 3 )); then
  echo "usage: $0 APP EXE SDL2_FRAMEWORK" >&2
  exit 2
fi
app=$1
exe=$2
fw=$3

if [[ ! -f "$app/Contents/Resources/disc.gcm" ]]; then
  echo "$app/Contents/Resources/disc.gcm is missing (tools/bundle_disc.py --image-only writes it)" >&2
  exit 1
fi
if [[ ! -f "$app/Contents/Resources/SMS.icns" ]]; then
  echo "$app/Contents/Resources/SMS.icns is missing (tools/extract_icon.py writes it)" >&2
  exit 1
fi
if [[ ! -f "$fw/SDL2" ]]; then
  echo "$fw is not an SDL2.framework" >&2
  exit 1
fi

mkdir -p "$app/Contents/MacOS" "$app/Contents/Frameworks"
cp "$exe" "$app/Contents/MacOS/sms"
rm -rf "$app/Contents/Frameworks/SDL2.framework"
# ditto keeps the framework's symlinks and its signature intact.
ditto "$fw" "$app/Contents/Frameworks/SDL2.framework"

cat > "$app/Contents/Info.plist" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleDevelopmentRegion</key>
	<string>en</string>
	<key>CFBundleExecutable</key>
	<string>sms</string>
	<key>CFBundleIconFile</key>
	<string>SMS</string>
	<key>CFBundleIdentifier</key>
	<string>io.github.chasem-dev.sms-port</string>
	<key>CFBundleInfoDictionaryVersion</key>
	<string>6.0</string>
	<key>CFBundleName</key>
	<string>SMS</string>
	<key>CFBundlePackageType</key>
	<string>APPL</string>
	<key>CFBundleShortVersionString</key>
	<string>0.1</string>
	<key>CFBundleVersion</key>
	<string>1</string>
	<key>LSApplicationCategoryType</key>
	<string>public.app-category.games</string>
	<key>NSHighResolutionCapable</key>
	<true/>
</dict>
</plist>
EOF

# Ad hoc: no Developer ID, so a downloaded copy still needs its quarantine
# attribute removed (BUILD.md#macos-app). The signature seals the disc image.
codesign --force --sign - --timestamp=none "$app"
codesign --verify --deep --strict "$app"
echo "Built $app"
