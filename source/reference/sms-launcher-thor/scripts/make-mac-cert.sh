#!/usr/bin/env bash
# Creates the self-signed certificate that signs Mac releases. Run it once and keep
# the output: installs only accept updates signed with this same certificate.
set -euo pipefail

out="${1:-$HOME/SMS Launcher Signing}"
name="SMS Launcher Self-Signed"
if [[ -e "$out/mac-signing.p12" ]]; then
  echo "$out/mac-signing.p12 already exists. Reuse it; a new certificate breaks updates for every install." >&2
  exit 1
fi
mkdir -p "$out"
chmod 700 "$out"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

cat > "$work/cert.cnf" <<EOF
[req]
distinguished_name=dn
prompt=no
x509_extensions=ext
[dn]
CN=$name
[ext]
basicConstraints=critical,CA:false
keyUsage=critical,digitalSignature
extendedKeyUsage=critical,codeSigning
subjectKeyIdentifier=hash
EOF

# 30 years: an expired certificate would also stop updates.
openssl req -x509 -newkey rsa:3072 -nodes -days 10950 -config "$work/cert.cnf" \
  -keyout "$work/key.pem" -out "$out/mac-signing.pem" 2>/dev/null
password="$(openssl rand -hex 24)"
# macOS's security tool cannot read OpenSSL 3's default PKCS#12 encryption.
legacy=()
openssl version | grep -q '^OpenSSL 3' && legacy=(-legacy)
openssl pkcs12 -export "${legacy[@]}" -name "$name" -inkey "$work/key.pem" -in "$out/mac-signing.pem" \
  -out "$out/mac-signing.p12" -passout "pass:$password"
printf '%s\n' "$password" > "$out/mac-signing-password.txt"
base64 -i "$out/mac-signing.p12" > "$out/MAC_CSC_LINK.txt"
chmod 600 "$out"/*

cat <<EOF
Created $out
  mac-signing.p12            certificate and private key
  mac-signing-password.txt   its password
  MAC_CSC_LINK.txt           the .p12 as base64

Add these GitHub Actions secrets to the launcher repository:
  MAC_CSC_LINK          contents of MAC_CSC_LINK.txt
  MAC_CSC_KEY_PASSWORD  contents of mac-signing-password.txt

Back this folder up somewhere safe. Losing it means every install must be reinstalled by hand.
EOF
