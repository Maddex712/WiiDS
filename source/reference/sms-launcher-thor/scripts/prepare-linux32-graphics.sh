#!/usr/bin/env bash
# Publisher-only: runs inside a disposable Debian container, never on a user's OS.
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive
dpkg --add-architecture i386
sed -i 's/^Types: deb$/Types: deb deb-src/' /etc/apt/sources.list.d/debian.sources
apt-get update -qq
apt-get install -y --no-install-recommends libsdl2-dev:i386 libegl-dev:i386 libgl-dev:i386 libgl1-mesa-dri:i386
mkdir -p /out/usr/lib /out/lib /out/usr/include /out/usr/share/doc /out/package-sources
cp -a /usr/lib/i386-linux-gnu /out/usr/lib/
cp -a /lib/i386-linux-gnu /out/lib/
for name in SDL2 EGL GL KHR GLES GLES2 GLES3 i386-linux-gnu/SDL2; do
  if [[ -d "/usr/include/$name" ]]; then
    mkdir -p "/out/usr/include/$(dirname "$name")"
    cp -a "/usr/include/$name" "/out/usr/include/$(dirname "$name")/"
  fi
done
cp -a /usr/share/glvnd /out/usr/share/
# Convert distribution absolute aliases to links inside the shipped tree.
while IFS= read -r -d '' file; do
  target=$(readlink "$file")
  if [[ "$target" == /* ]]; then
    if [[ -e "/out$target" ]]; then
      ln -snf "$(realpath --relative-to="$(dirname "$file")" "/out$target")" "$file"
    else rm "$file"; fi
  fi
done < <(find /out -type l -print0)
dpkg-query -W -f='${binary:Package}\t${Version}\t${source:Package}\t${source:Version}\t${Architecture}\t${db:Status-Abbrev}\n' \
  | awk -F '\t' '$5 == "i386" && $6 ~ /^ii/ {print $1 "\t" $2 "\t" $3 "\t" $4}' > /out/packages.tsv
while IFS=$'\t' read -r package version source source_version; do
  name=${package%:i386}
  if [[ -e "/usr/share/doc/$name/copyright" ]]; then
    mkdir -p "/out/usr/share/doc/$name"
    cp -L "/usr/share/doc/$name/copyright" "/out/usr/share/doc/$name/"
  fi
  if [[ ! -d "/out/package-sources/$source" ]]; then
    mkdir -p "/out/package-sources/$source"
    (cd "/out/package-sources/$source" && apt-get source --download-only --only-source "$source=$source_version")
  fi
done < /out/packages.tsv
chown -R "$SMS_PUBLISH_UID:$SMS_PUBLISH_GID" /out
