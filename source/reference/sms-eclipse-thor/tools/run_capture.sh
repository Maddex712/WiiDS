#!/bin/sh
# Run build/linux-$SMS_ARCH/sms (SMS_ARCH default 32, or SMS_BUILD=dir) headless
# for SECONDS, capturing the listed retail fields, then convert them to
# build/shots/ and compare with the retail captures.
# usage: tools/run_capture.sh SECONDS FIELD,FIELD,... [extra sms args]
root=$(cd "$(dirname "$0")/.." && pwd)
secs=${1:-60}; fields=${2:-236,300,460,600,800,1000,1200,1500,1800,2100}
shift 2 2>/dev/null
rm -rf "$root/build/shots-raw"
cd "$root/${SMS_BUILD:-build/linux-${SMS_ARCH:-32}}" && SMS_QUIET_STUBS=${SMS_QUIET_STUBS:-1} SMS_SHOTS=$fields SMS_SHOT_DIR="$root/build/shots-raw" \
  timeout "$secs" ./sms --headless "$@" > run.log 2>&1
echo "exit $?"
grep -v '^\[OSReport\] *$\|libEGL\|pci id\|^$\|^\[stub\]  ' run.log | tail -12
if grep -q "fatal signal" run.log; then
  addr2line -f -C -e sms $(grep -o "sms(+0x[0-9a-f]*)" run.log | sed 's/sms(+\(.*\))/\1/' | sed -n 2,7p) | paste - - | sed "s|$root/||"
fi
python3 "$root/tools/shots.py"
