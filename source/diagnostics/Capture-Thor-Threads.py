"""Two read-only ADB /proc thread snapshots; no run-as or device changes.

CPU ticks and scheduler counters can distinguish activity from a wait, but these
non-atomic samples do not establish why a worker stalled or measure guest FPS.
"""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parent.parent
FIELDS = ('comm', 'stat', 'schedstat', 'wchan', 'cgroup', 'cpuset', 'status')
LIMIT = 10


def parse_stat(text):
    # comm can contain spaces, '(' and ')': numeric positions begin only after
    # the LAST closing parenthesis, not at text.split()[2]. Kernel fields 3+.
    match = re.fullmatch(r"\s*(\d+) \((.*)\) (\S)(?: (.*))?\s*", text.rstrip('\n'), re.S)
    if not match: raise ValueError('Malformed proc stat framing')
    fields = [match[3], *(match[4] or '').split()]
    if len(fields) < 37: raise ValueError('Truncated proc stat before field 39 processor')
    at = lambda field: int(fields[field - 3])
    result = dict(tid=int(match[1]), name=match[2], state=fields[0], user_ticks=at(14),
                  system_ticks=at(15), start_ticks=at(22), current_core=at(39))
    if any(result[n] < 0 for n in ('user_ticks', 'system_ticks', 'start_ticks')):
        raise ValueError('Negative proc CPU/start counters')
    result['cpu_ticks'] = result['user_ticks'] + result['system_ticks']
    return result


def parse_schedstat(text):
    parts = text.split()
    if len(parts) != 3 or not all(p.isdigit() for p in parts):
        raise ValueError('Expected three unsigned schedstat counters')
    return dict(runtime_ns=int(parts[0]), runnable_wait_ns=int(parts[1]), timeslices=int(parts[2]))


def parse_frames(text):
    rows = {}
    pattern = re.compile(r'^@@THOR_BEGIN\t(\d+)\t([a-z]+)\n(.*?)\n@@THOR_END\t(-?\d+)\n', re.M | re.S)
    for match in pattern.finditer(text.replace('\r\n', '\n')):
        tid, field = int(match[1]), match[2]
        if field not in FIELDS or (tid, field) in rows: raise ValueError('Duplicate/unexpected framed proc field')
        rows[tid, field] = {'returncode': int(match[4]), 'raw_text': match[3]}
    return rows


def frame_read(tid, field, path):
    return (f"printf '@@THOR_BEGIN\\t{tid}\\t{field}\\n'; cat {shlex.quote(path)} 2>&1; r=$?; "
            "printf '\\n@@THOR_END\\t%s\\n' \"$r\"" )


def discovery_script(pid):
    if not isinstance(pid, int) or pid <= 0: raise ValueError('Invalid PID')
    return (f'ls -d /proc/{pid}/task/* >/dev/null || exit $?; '
            f'for p in /proc/{pid}/task/*/comm; do '
            '[ -e "$p" ] || continue; t=${p%/comm}; t=${t##*/}; '
            "printf '@@THOR_BEGIN\\t%s\\tcomm\\n' \"$t\"; cat \"$p\" 2>&1; r=$?; "
            "printf '\\n@@THOR_END\\t%s\\n' \"$r\"; done; exit 0")


def select_threads(rows):
    selected = []
    for (tid, field), row in rows.items():
        if field != 'comm' or row['returncode'] != 0: continue
        name = row['raw_text'].rstrip('\n')
        lowered = name.lower()
        priority = (0 if lowered == 'sdlthread' else 1 if lowered == 'frameworker' else
                    2 if lowered == 'presenter' else 3 if lowered in ('gxworker', 'fifoworker') else
                    4 if any(part in lowered for part in ('render', 'fifo', 'vulkan', 'gpu', 'dawn')) else None)
        if priority is not None: selected.append((priority, tid, name))
    return [{'tid': tid, 'name': name} for _, tid, name in sorted(selected)[:LIMIT]]


def sample_script(pid, threads):
    if not isinstance(pid, int) or pid <= 0 or len(threads) > LIMIT: raise ValueError('Invalid sample selection')
    lines = []
    for thread in threads:
        tid = thread['tid']
        if not isinstance(tid, int) or tid <= 0: raise ValueError('Invalid TID')
        lines += [frame_read(tid, f, f'/proc/{pid}/task/{tid}/{f}') for f in FIELDS]
    return '\n'.join(lines) + '\nexit 0'


def decode_threads(rows, threads):
    result = []
    for thread in threads:
        entry = dict(thread, fields={})
        for name in FIELDS:
            row = rows.get((thread['tid'], name))
            field = dict(row or {}, status='unavailable')
            entry['fields'][name] = field
            if row is None: field['error'] = 'No framed response'; continue
            if row['returncode'] != 0: field['error'] = row['raw_text'].strip(); continue
            try:
                raw = row['raw_text']
                if name == 'stat':
                    field['value'] = parse_stat(raw)
                    if field['value']['tid'] != thread['tid']: raise ValueError('Stat TID differs from selected thread')
                elif name == 'schedstat': field['value'] = parse_schedstat(raw)
                elif name == 'cgroup':
                    field['value'] = [dict(zip(('hierarchy', 'controllers', 'path'), line.split(':', 2)))
                                      for line in raw.splitlines() if len(line.split(':', 2)) == 3]
                elif name == 'status':
                    wanted = {'Cpus_allowed_list', 'Mems_allowed_list', 'voluntary_ctxt_switches', 'nonvoluntary_ctxt_switches'}
                    field['value'] = {k: v.strip() for line in raw.splitlines() if ':' in line
                                      for k, v in [line.split(':', 1)] if k in wanted}
                else: field['value'] = raw.strip()
                field['status'] = 'ok'
                if name == 'wchan' and raw.strip() == '0': field['note'] = 'May be running, unavailable or kernel-redacted; not proof of no wait'
            except (ValueError, IndexError) as error:
                field.update(status='invalid', error=str(error))
        result.append(entry)
    return result


def comparisons(first, second):
    if first.get('pid') != second.get('pid') or not first.get('pid'):
        return {'status': 'process_changed_or_unavailable', 'threads': []}
    by_tid = {row['tid']: row for row in first.get('threads', [])}
    result = []
    for row in second.get('threads', []):
        before = by_tid.get(row['tid'])
        a = before['fields']['stat'].get('value') if before else None
        b = row['fields']['stat'].get('value')
        out = dict(tid=row['tid'], name=row['name'], status='not_comparable')
        if a and b and a['tid'] == b['tid'] == row['tid'] and a['start_ticks'] == b['start_ticks'] and a['name'] == b['name']:
            changes = {n: b[n] - a[n] for n in ('user_ticks', 'system_ticks', 'cpu_ticks')}
            if min(changes.values()) >= 0:
                out.update(status='comparable', delta=changes, state_before=a['state'], state_after=b['state'],
                           core_before=a['current_core'], core_after=b['current_core'])
                x = before['fields']['schedstat'].get('value'); y = row['fields']['schedstat'].get('value')
                if x and y:
                    delta = {k: y[k] - x[k] for k in x}
                    if min(delta.values()) >= 0: out['schedstat_delta'] = delta
            else: out['error'] = 'Counters decreased'
        result.append(out)
    return {'status': 'same_process', 'threads': result}


def command(args, remote):
    started = time.monotonic()
    try:
        r = subprocess.run([args.adb, '-s', args.serial, 'shell', shlex.join(remote)],
                           capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=15)
        return dict(remote_argv=remote, returncode=r.returncode, stdout=r.stdout, stderr=r.stderr,
                    started_monotonic=started, finished_monotonic=time.monotonic())
    except (OSError, subprocess.TimeoutExpired) as error:
        decode = lambda x: x.decode('utf-8', 'replace') if isinstance(x, bytes) else x or ''
        return dict(remote_argv=remote, returncode=None, error=str(error), stdout=decode(getattr(error, 'stdout', '')),
                    stderr=decode(getattr(error, 'stderr', '')), started_monotonic=started, finished_monotonic=time.monotonic())


def snapshot(args, index):
    sample = dict(index=index, utc=datetime.now(timezone.utc).isoformat(), threads=[])
    def capture(label, remote):
        result = command(args, remote)
        (args.raw / f'{index}-{label}.json').write_text(json.dumps(result, indent=2) + '\n')
        return result
    found = capture('pid', ['pidof', args.package])
    pids = found['stdout'].split()
    if found['returncode'] != 0 or len(pids) != 1 or not pids[0].isdigit():
        return dict(sample, status='pid_unavailable_or_ambiguous', pid_response=found)
    pid = int(pids[0]); sample['pid'] = pid
    discovered = capture('discovery', ['sh', '-c', discovery_script(pid)])
    try: rows = parse_frames(discovered['stdout']) if discovered['returncode'] == 0 else {}
    except ValueError as error: return dict(sample, status='invalid_discovery', error=str(error))
    selected = select_threads(rows)
    sample['discovery_errors'] = [dict(tid=t, field=f, **r) for (t, f), r in rows.items() if r['returncode'] != 0]
    sample['selected'] = selected
    if not selected: return dict(sample, status='no_readable_relevant_threads')
    response = capture('fields', ['sh', '-c', sample_script(pid, selected)])
    sample['started_monotonic'] = response['started_monotonic']; sample['finished_monotonic'] = response['finished_monotonic']
    try: fields = parse_frames(response['stdout']) if response['returncode'] == 0 else {}
    except ValueError as error: return dict(sample, status='invalid_fields', error=str(error))
    sample['threads'] = decode_threads(fields, selected)
    sample['status'] = 'captured' if all(f['status'] == 'ok' for t in sample['threads'] for f in t['fields'].values()) else 'partial_or_unavailable'
    return sample


def arguments():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial', required=True)
    parser.add_argument('--package', required=True)
    parser.add_argument('--output', type=Path, required=True, help='Fresh .json path; raw command responses go in adjacent .samples/')
    parser.add_argument('--interval', type=float, default=1.0, help='Target seconds between snapshot starts (0.1..10; default 1)')
    parser.add_argument('--adb', help='Existing ADB executable; no installation/download')
    args = parser.parse_args()
    if not args.serial or re.search(r'\s', args.serial): parser.error('serial must be nonempty without whitespace')
    if not re.fullmatch(r'[A-Za-z_]\w*(?:\.[A-Za-z_]\w*)+', args.package, re.ASCII): parser.error('invalid package name')
    if not 0.1 <= args.interval <= 10: parser.error('interval must be finite and within 0.1..10 seconds')
    args.output = args.output.resolve(); args.raw = args.output.with_name(args.output.stem + '.samples')
    if args.output.suffix.lower() != '.json' or args.output.exists() or args.raw.exists() or args.output.is_symlink() or args.raw.is_symlink(): parser.error('choose a fresh .json output and .samples directory')
    bundled = ROOT / '.tools/platform-tools/adb.exe'
    args.adb = shutil.which(args.adb or (str(bundled) if bundled.is_file() else 'adb'))
    if not args.adb: parser.error('ADB not found; provide --adb')
    return args


def main():
    args = arguments(); args.output.parent.mkdir(parents=True, exist_ok=True); args.raw.mkdir()
    hz = command(args, ['getconf', 'CLK_TCK'])
    (args.raw / 'clock-ticks.json').write_text(json.dumps(hz, indent=2) + '\n')
    rate = int(hz['stdout'].strip()) if hz['returncode'] == 0 and hz['stdout'].strip().isdigit() and int(hz['stdout'].strip()) > 0 else None
    start = time.monotonic(); first = snapshot(args, 0)
    time.sleep(max(0, start + args.interval - time.monotonic()))
    second = snapshot(args, 1)
    gap = None
    if first.get('started_monotonic') is not None and second.get('started_monotonic') is not None:
        gap = second['started_monotonic'] - first['started_monotonic']
    report = dict(schema=1, serial=args.serial, package=args.package, read_only=True, thread_limit=LIMIT,
                  requested_interval_seconds=args.interval, observed_field_start_gap_seconds=gap,
                  remote_clock_ticks_per_second=rate, samples=[first, second], comparison=comparisons(first, second),
                  raw_responses=str(args.raw),
                  limits=['Non-atomic /proc reads; permissions, unavailable fields and transport errors retained.',
                          'Current core is the last reported processor, not a continuous affinity/placement trace.',
                          'CPU/scheduler deltas and wchan do not identify stall cause or measure guest FPS.',
                          'Zero schedstat counters may mean accounting is disabled. No run-as, root, settings or scheduler changes.'])
    with args.output.open('x') as stream: stream.write(json.dumps(report, indent=2) + '\n')
    print(args.output)


if __name__ == '__main__': main()
