"""Bounded host fixtures only: never invoke ADB or contact a device."""
import copy
import importlib.util
from pathlib import Path
import shlex
import unittest
import tempfile
import json
import subprocess
import shutil
from types import SimpleNamespace
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('thor_threads', Path(__file__).with_name('Capture-Thor-Threads.py'))
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
POSIX_SHELL = shutil.which('sh')


def stat(tid=42, name='SDLThread', user=111, system=222, start=333, core=7):
    fields = {n: str(1000 + n) for n in range(3, 53)}
    fields.update({3: 'S', 14: str(user), 15: str(system), 22: str(start), 39: str(core)})
    return f'{tid} ({name}) ' + ' '.join(fields[n] for n in range(3, 53)) + '\n'


def frame(tid, field, text, code=0):
    return f'@@THOR_BEGIN\t{tid}\t{field}\n{text}\n@@THOR_END\t{code}\n'


def sample(user=111, start=333, pid=10):
    selected = [{'tid': 42, 'name': 'SDLThread'}]
    rows = m.parse_frames(frame(42, 'stat', stat(user=user, start=start)) +
                          frame(42, 'schedstat', '10000 20000 5\n'))
    return {'pid': pid, 'threads': m.decode_threads(rows, selected)}


class ThreadTests(unittest.TestCase):
    def test_stat_parentheses_spaces_newline_and_exact_indexes(self):
        for name in ['SDLThread', 'Frame (Worker)', 'a ) b (c)', 'a\nb)']:
            value = m.parse_stat(stat(name=name))
            self.assertEqual((42, name, 'S', 111, 222, 333, 7, 333),
                tuple(value[k] for k in ('tid', 'name', 'state', 'user_ticks', 'system_ticks', 'start_ticks', 'current_core', 'cpu_ticks')))

    def test_stat_bad_or_truncated_is_not_guessed(self):
        for raw in ['42 no parentheses S 1', '42 (worker) S 1 2', stat(user=-1), stat().replace(' 111 ', ' bad '), '']:
            with self.assertRaises(ValueError): m.parse_stat(raw)
        with self.assertRaises(ValueError): m.parse_schedstat('10 20 nope')

    def test_frames_permission_missing_and_redacted_wchan_preserved(self):
        raw = frame(42, 'stat', stat()) + frame(42, 'schedstat', 'cat: Permission denied\n', 1) + frame(42, 'wchan', '0')
        rows = m.parse_frames(raw)
        thread = m.decode_threads(rows, [{'tid': 42, 'name': 'SDLThread'}])[0]
        self.assertEqual('ok', thread['fields']['stat']['status'])
        self.assertEqual('unavailable', thread['fields']['schedstat']['status'])
        self.assertIn('Permission denied', thread['fields']['schedstat']['error'])
        self.assertEqual('unavailable', thread['fields']['cpuset']['status'])
        self.assertIn('redacted', thread['fields']['wchan']['note'])
        with self.assertRaises(ValueError): m.parse_frames(raw + frame(42, 'wchan', 'new'))

    def test_name_selection_is_bounded_and_keeps_primary_workers(self):
        names = ['RenderThread' + str(i) for i in range(20)] + ['SDLThread', 'Presenter', 'FrameWorker', 'GxWorker', 'Jit thread pool']
        rows = m.parse_frames(''.join(frame(i + 10, 'comm', n + '\n') for i, n in enumerate(names)))
        selected = m.select_threads(rows)
        self.assertEqual(10, len(selected))
        self.assertEqual(['SDLThread', 'FrameWorker', 'Presenter', 'GxWorker'], [v['name'] for v in selected[:4]])
        script = m.sample_script(99, selected)
        self.assertNotIn('run-as', script)
        self.assertNotIn('setprop', script)
        self.assertEqual(70, script.count('@@THOR_BEGIN'))
        with self.assertRaises(ValueError): m.sample_script(99, [{'tid': '1;bad'}])
        remote = ['sh', '-c', script]
        self.assertEqual(remote, shlex.split(shlex.join(remote)))

    def test_cpuset_cgroup_status_and_schedstat_units(self):
        raw = frame(1, 'cpuset', '/top-app\n') + frame(1, 'cgroup', '0::/uid_10300/pid_10\n4:cpuset:/top-app\n')
        raw += frame(1, 'status', 'Cpus_allowed_list:\t0-7\nMems_allowed_list:\t0\nName:\tignored\n')
        value = m.decode_threads(m.parse_frames(raw), [{'tid': 1, 'name': 'Presenter'}])[0]['fields']
        self.assertEqual('/top-app', value['cpuset']['value'])
        self.assertEqual('/uid_10300/pid_10', value['cgroup']['value'][0]['path'])
        self.assertEqual({'Cpus_allowed_list': '0-7', 'Mems_allowed_list': '0'}, value['status']['value'])
        self.assertEqual({'runtime_ns': 10, 'runnable_wait_ns': 20, 'timeslices': 3}, m.parse_schedstat('10 20 3'))

    def test_deltas_require_same_process_thread_start_and_nonnegative_ticks(self):
        a, b = sample(), sample(user=118)
        result = m.comparisons(a, b)['threads'][0]
        self.assertEqual('comparable', result['status'])
        self.assertEqual(7, result['delta']['cpu_ticks'])
        self.assertEqual('process_changed_or_unavailable', m.comparisons(a, sample(pid=11))['status'])
        self.assertEqual('not_comparable', m.comparisons(a, sample(start=999))['threads'][0]['status'])
        self.assertEqual('not_comparable', m.comparisons(a, sample(user=1))['threads'][0]['status'])

    def test_snapshot_mocked_transport_retains_raw_errors(self):
        def response(text, code=0):
            return dict(stdout=text, stderr="", returncode=code, started_monotonic=1.0, finished_monotonic=1.1)
        outputs = [response('123\n'), response(frame(42, 'comm', 'SDLThread\n')),
                   response(frame(42, 'stat', stat()) + frame(42, 'cpuset', 'Permission denied\n', 1))]
        with tempfile.TemporaryDirectory() as folder:
            args = SimpleNamespace(raw=Path(folder), package='com.example.app')
            with patch.object(m, 'command', side_effect=outputs) as command:
                result = m.snapshot(args, 0)
            self.assertEqual(3, command.call_count)
            self.assertEqual('partial_or_unavailable', result['status'])
            self.assertEqual('unavailable', result['threads'][0]['fields']['cpuset']['status'])
            raw = json.loads((Path(folder) / '0-fields.json').read_text())
            self.assertIn('Permission denied', raw['stdout'])
            self.assertEqual(3, len(list(Path(folder).glob('*.json'))))

    def test_fresh_output_and_package_rejection_before_any_adb(self):
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder) / 'taken.json'; output.write_text('existing')
            for package, path in [('bad;package', Path(folder)/'new.json'), ('com.example.app', output)]:
                with patch('sys.argv', ['capture', '--serial', 'fixture', '--package', package, '--output', str(path)]):
                    with patch.object(m, 'command') as command, self.assertRaises(SystemExit): m.arguments()
                    command.assert_not_called()
            self.assertEqual('existing', output.read_text())

    @unittest.skipUnless(POSIX_SHELL, 'Local POSIX shell unavailable')
    def test_real_local_shell_framing_and_filename_quoting(self):
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder) / "fixture with ' quotes.txt"; source.write_text('0')
            script = m.frame_read(7, 'wchan', source.as_posix())
            response = subprocess.run([POSIX_SHELL, '-c', script], capture_output=True, text=True, check=True)
            self.assertEqual({'returncode': 0, 'raw_text': '0'}, m.parse_frames(response.stdout)[7, 'wchan'])


if __name__ == '__main__': unittest.main()
