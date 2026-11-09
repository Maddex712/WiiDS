#!/bin/sh
# Builds ANGLE -- OpenGL ES over Metal -- for visionOS, which has no GL of its own: Apple's
# OpenGL ES is there only for iPad apps run in compatibility, and a native app cannot
# create an EAGLContext. The renderer runs on ANGLE there instead (visionos/).
#
# From WebKit's copy of ANGLE rather than Chromium's: WebKit ships it on visionOS and
# carries an Xcode project that builds it for every Apple platform, where Chromium's GN
# build knows only iOS and tvOS. A sparse checkout of the few directories that project
# needs, at a pinned commit, is about 250 MB; the build takes a few minutes.
#
#   build-angle-visionos.sh <out_dir> [xros|xrsimulator]
#
# leaves <out_dir>/libANGLE-shared.dylib (installed as @rpath/libANGLE-shared.dylib) and
# <out_dir>/include. WebKit's build prefixes the entry points: eglFoo is exported as
# EGL_Foo and glFoo as GL_Foo, so they are looked up by those names (visionos/angle.mm).
set -e
OUT=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
SDK=${2:-xros}
WEBKIT_COMMIT=3a41ad1d523ccc432418410723dfe5e60f1f9a63
SRC=$OUT/webkit
BUILD=$OUT/build
export DEVELOPER_DIR=${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}

mkdir -p "$OUT"
if [ ! -d "$SRC/.git" ]; then
  git clone --filter=blob:none --sparse --no-checkout https://github.com/WebKit/WebKit.git "$SRC"
  git -C "$SRC" sparse-checkout set Source/ThirdParty/ANGLE Configurations Tools/ccache WebKitLibraries/SDKs
  git -C "$SRC" fetch --depth 1 --filter=blob:none origin "$WEBKIT_COMMIT"
  git -C "$SRC" checkout -q FETCH_HEAD
fi

# Xcode 26 ships the Metal compiler separately, and ANGLE compiles its internal shaders.
if ! xcrun -f metal >/dev/null 2>&1 || ! xcrun metal -v >/dev/null 2>&1; then
  xcodebuild -downloadComponent MetalToolchain
fi

PROJ="$SRC/Source/ThirdParty/ANGLE/ANGLE.xcodeproj"
CONF=Release
# The project's "Public SDK gate" waits for two things a full WebKit build makes first: the
# platform's SDK additions in the products directory, and a clang overlay that defuses
# availability checks. The additions are in the checkout; an empty overlay will do.
ADDITIONS=$(xcodebuild -project "$PROJ" -target "ANGLE (dynamic)" -configuration $CONF -sdk "$SDK" \
  SYMROOT="$BUILD" -showBuildSettings 2>/dev/null | awk -v k="SDK_DIR_WebKitSDKAdditions_$SDK" '$1 == k {print $3}')
mkdir -p "$BUILD/$CONF-$SDK"
ln -sfn "$ADDITIONS" "$BUILD/$CONF-$SDK/SDKAdditions"
printf "{ 'version': 0, 'roots': [] }\n" > "$BUILD/availability-overlay.yaml"

xcodebuild -project "$PROJ" -target "ANGLE (dynamic)" -configuration $CONF -sdk "$SDK" \
  SYMROOT="$BUILD" CODE_SIGNING_ALLOWED=NO -quiet build

cp "$BUILD/$CONF-$SDK/libANGLE-shared.dylib" "$OUT/libANGLE-shared.dylib"
install_name_tool -id @rpath/libANGLE-shared.dylib "$OUT/libANGLE-shared.dylib"
rm -rf "$OUT/include"
cp -R "$SRC/Source/ThirdParty/ANGLE/include" "$OUT/include"
echo "ANGLE for $SDK in $OUT"
