#!/bin/sh
# Build the Vision Pro app, and with --install, install it on a paired Vision Pro.
#
#   ./build-visionos.sh                 configure (first run) and build build-visionos/
#   ./build-visionos.sh --install       ...then install it on the Vision Pro
#   ./build-visionos.sh --push-image    ...then copy rom/'s disc image into the app
#
# Signing uses the Apple development team in $GCN_APPLE_TEAM (default Y3GFBT2WQ2); Xcode
# makes the provisioning profile. The Vision Pro must be paired with this Mac (Xcode >
# Devices and Simulators) and in developer mode; $GCN_DEVICE names it when more than one
# device is paired. Xcode builds for it only once it has the visionOS platform matching its
# SDK (xcodebuild -downloadPlatform visionOS).
set -e
cd "$(dirname "$0")"
if [ ! -f gcn-recomp/CMakeLists.txt ]; then
  git submodule update --init
fi
export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
TEAM=${GCN_APPLE_TEAM:-Y3GFBT2WQ2}
BUNDLE=com.ethannicholas.mkdd
# Configured again whenever a CMake file is newer than the project. Left to Xcode, the
# change is noticed part way through a build, which regenerates the project and finishes
# with the old one: an app built that way went out without its shader key list.
PBX=build-visionos/mkdd.xcodeproj/project.pbxproj
if [ ! -f "$PBX" ] || [ -n "$(find CMakeLists.txt gcn-recomp/CMakeLists.txt gcn-recomp/tools -name 'CMakeLists.txt' -newer "$PBX" -o -name '*.cmake' -newer "$PBX" 2>/dev/null)" ]; then
  cmake -S . -B build-visionos -G Xcode -DCMAKE_SYSTEM_NAME=visionOS \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=26.0 -DGCN_APPLE_TEAM="$TEAM"
  touch "$PBX"  # CMake leaves an unchanged project alone, which would configure every run
fi

device() {
  if [ -n "$GCN_DEVICE" ]; then echo "$GCN_DEVICE"; return; fi
  xcrun devicectl list devices --hide-headers --hide-default-columns --columns Identifier --columns Model \
    | awk '/RealityDevice|Vision/ {print $1; exit}'
}

# Built for the paired Vision Pro when there is one, so that Xcode registers it with the team
# and puts it in the provisioning profile; a profile without it cannot be installed on it.
DEST="generic/platform=visionOS"
DEV=$(device)
if [ -n "$DEV" ]; then
  UDID=$(xcrun devicectl device info details --device "$DEV" 2>/dev/null | awk '$2 == "udid:" {print $3; exit}')
  [ -n "$UDID" ] && DEST="id=$UDID"
fi
# A scheme, not -target: a target build ignores -destination.
xcodebuild -project build-visionos/mkdd.xcodeproj -scheme mkdd -configuration RelWithDebInfo \
  SYMROOT="$PWD/build-visionos" -destination "$DEST" -allowProvisioningUpdates -allowProvisioningDeviceRegistration -quiet build
APP=build-visionos/RelWithDebInfo-xros/mkdd.app
echo "built $APP"

for arg in "$@"; do
  case "$arg" in
  --install)
    [ -n "$DEV" ] || { echo "no Vision Pro paired" >&2; exit 1; }
    xcrun devicectl device install app --device "$DEV" "$APP"
    ;;
  --push-image)
    [ -n "$DEV" ] || { echo "no Vision Pro paired" >&2; exit 1; }
    IMG=$(ls rom/*.iso rom/*.ciso 2>/dev/null | head -1)
    [ -n "$IMG" ] || { echo "no disc image in rom/" >&2; exit 1; }
    EXT=${IMG##*.}
    xcrun devicectl device copy to --device "$DEV" --domain-type appDataContainer \
      --domain-identifier "$BUNDLE" --source "$IMG" --destination "Documents/game.$EXT"
    ;;
  esac
done
