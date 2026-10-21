#!/bin/sh
# Run build/linux-$SMS_ARCH/sms (SMS_ARCH default 32, or SMS_BUILD=dir) under gdb and print a backtrace at the first fatal signal.
cd "$(dirname "$0")/../${SMS_BUILD:-build/linux-${SMS_ARCH:-32}}" && timeout ${T:-120} gdb -q -batch -ex "set debuginfod enabled off" -ex "handle SIGSEGV stop" -ex "handle SIG34 nostop noprint" -ex run -ex "bt ${BT:-15}" -ex "info locals" --args ./sms "$@" 2>&1 | grep -v "^\[New Thread\|^\[Thread\|libthread_db\|^Using host"
