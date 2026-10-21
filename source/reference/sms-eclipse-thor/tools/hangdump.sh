#!/bin/sh
# Run build/linux-$SMS_ARCH/sms (SMS_ARCH default 32, or SMS_BUILD=dir) under gdb for N seconds (default 12), then dump every thread's
# game-level stack (host sync frames filtered out).
N=${1:-12}; shift 2>/dev/null
cd "$(dirname "$0")/../${SMS_BUILD:-build/linux-${SMS_ARCH:-32}}"
printf 'set debuginfod enabled off\nset pagination off\nhandle SIGALRM stop print\nhandle SIG34 nostop noprint\nrun\nthread apply all bt 14\n' > hang.gdb
(SMS_QUIET_STUBS=1 timeout $((N + 60)) gdb -q -batch -x hang.gdb --args ./sms "$@" > hang.txt 2>&1 &)
sleep "$N"; pkill -ALRM -x sms; sleep 4
grep -v "^\[New Thread\|^\[Thread\|libthread_db\|^Using host" hang.txt | grep -A16 "^Thread\|received signal" | grep -v "pthread_cond\|futex\|__GI\|libc.so\|__kernel_vsyscall\|___pthread\|clock_nanosleep\|switch_to\|reschedule\|host_entry\|JKRThread::start"
