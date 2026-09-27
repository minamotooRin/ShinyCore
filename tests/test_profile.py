"""Profiler contracts, pacing separation and native asynchronous GPU checks."""
import argparse
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('profile_report', ROOT/'tools/profile_report.py')
report = importlib.util.module_from_spec(spec)
spec.loader.exec_module(report)
BINARY = None
NATIVE = False


def fixture():
    header = {'type': 'header', 'version': 1, 'engine': 'test', 'graphics': True,
              'gpu_timer': 'gl_time_elapsed', 'trace_or_record': False}
    frames = [{'type': 'frame', 'frame': i, 'tick': i+1, 'elapsed_ms': i*20, 'wall_ms': 20,
               'cpu_ms': i+1, 'wait_ms': 10, 'diagnostic_ms': 0, 'steps': 1,
               'entities': 2000, 'particles': 20000, 'projectiles': 20000, 'draw_commands': 2,
               'gpu_issued': True, 'phases': {'physics_ms': 0.25}, 'resident_bytes': 100,
               'peak_resident_bytes': 200, 'projectile_hits_total': i*20} for i in range(5)]
    # Results arrive late; source frame, not arrival position, selects the interval.
    gpu = [{'type': 'gpu', 'frame': i, 'gpu_ms': 100 if i == 0 else 2} for i in range(4)]
    end = {'type': 'end', 'frames': 5, 'gpu_samples': 4, 'gpu_pending': 1, 'gpu_skipped': 0}
    return [header, *frames, *gpu, end]


class ProfileTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='shiny-profile-')
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)

    def write(self, rows):
        path = self.root/'profile.jsonl'
        path.write_text('\n'.join(map(json.dumps, rows))+'\n', encoding='utf-8')
        return path

    def args(self, **values):
        result = dict(warmup=0.02, duration=0.06, cpu_p99_max=4, gpu_p99_max=2,
                      cpu_max=None, rss_max_mib=1, minimum=[('projectiles', 20000)], min_hits_per_second=None)
        return argparse.Namespace(**(result | values))

    def test_delayed_gpu_is_matched_to_original_frame(self):
        data = report.summarize(self.write(fixture()), self.args())
        self.assertTrue(data['ok'], data['errors'])
        self.assertEqual(data['gpu_ms']['max'], 2)
        self.assertEqual(data['cpu_ms']['p99'], 4)
        self.assertEqual(data['gpu_missing'], 0)

    def test_failures_cannot_be_turned_into_zero_or_passes(self):
        rows = fixture()
        for args, message in [(self.args(duration=0), 'GPU samples missing'),
                              (self.args(duration=1), 'full measurement duration'),
                              (self.args(cpu_p99_max=3), 'CPU p99'),
                              (self.args(minimum=[('entities', 2001)]), 'entities fell below'),
                              (self.args(min_hits_per_second=1000), 'one-second')]:
            with self.subTest(message=message):
                data = report.summarize(self.write(rows), args)
                self.assertFalse(data['ok'])
                self.assertTrue(any(message in e for e in data['errors']), data['errors'])
        rows[0]['trace_or_record'] = True
        self.assertIn('disable trace/record', report.summarize(self.write(rows), self.args())['errors'][0])
        rows = fixture()
        rows[2]['peak_resident_bytes'] = None
        self.assertIn('memory unavailable', report.summarize(self.write(rows), self.args())['errors'][0])

    def test_malformed_or_truncated_records_fail(self):
        cases = [fixture()[:-1]]
        rows = fixture(); rows.insert(-1, rows[-2]); cases.append(rows)
        rows = fixture(); rows[2]['cpu_ms'] = float('nan'); cases.append(rows)
        rows = fixture(); rows[2]['frame'] = 20; cases.append(rows)
        rows = fixture(); rows[-1]['gpu_pending'] = 0; cases.append(rows)
        for rows in cases:
            with self.subTest(rows=rows):
                with self.assertRaises(ValueError):
                    report.load(self.write(rows))

    def test_percentiles_use_nearest_rank(self):
        self.assertEqual(report.distribution(list(range(1, 101))),
                         {'p50': 50, 'p95': 95, 'p99': 99, 'max': 100, 'samples': 100})
        self.assertIsNone(report.distribution([]))

    def test_hit_rate_checks_each_second_and_counter_reset(self):
        header = fixture()[0] | {'graphics': False, 'gpu_timer': 'headless'}
        base = fixture()[1]
        frames = [base | {'frame': i, 'tick': i+1, 'elapsed_ms': i*20,
                         'gpu_issued': False, 'projectile_hits_total': i*20} for i in range(105)]
        end = {'type': 'end', 'frames': 105, 'gpu_samples': 0, 'gpu_pending': 0, 'gpu_skipped': 0}
        args = self.args(warmup=0, duration=2.1, gpu_p99_max=None, min_hits_per_second=1000)
        self.assertTrue(report.summarize(self.write([header, *frames, end]), args)['ok'])
        for frame in frames[51:]:
            frame['projectile_hits_total'] = 1000
        errors = report.summarize(self.write([header, *frames, end]), args)['errors']
        self.assertTrue(any('hit rate below' in e for e in errors))
        frames[70]['projectile_hits_total'] = 0
        errors = report.summarize(self.write([header, *frames, end]), args)['errors']
        self.assertTrue(any('counter reset' in e for e in errors))

    def run_game(self, *options):
        result = subprocess.run([str(BINARY), str(ROOT/'examples/lantern'), '--mute', *map(str, options)],
                                capture_output=True, text=True, encoding='utf-8', timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads(result.stdout)

    def test_real_headless_profile_and_pacing_preserve_state(self):
        if not BINARY:
            self.skipTest('pass an engine binary')
        plain = self.run_game('--headless', '--frames', 8)
        path = self.root/'paced.jsonl'
        paced = self.run_game('--headless', '--frames', 8, '--realtime', '--profile', path)
        self.assertEqual(plain, paced)
        header, frames, gpu, end = report.load(path)
        self.assertEqual(header['gpu_timer'], 'headless')
        self.assertFalse(gpu)
        self.assertEqual(end['frames'], 8)
        self.assertGreater(sum(f['wait_ms'] for f in frames), 50)
        for f in frames:
            self.assertAlmostEqual(f['cpu_ms']+f['wait_ms']+f['diagnostic_ms'], f['wall_ms'], places=4)
            self.assertGreaterEqual(f['phases']['simulation_ms'], f['phases']['physics_ms'])
            self.assertEqual(f['steps'], 1)
            self.assertIsNone(f['gpu_draw_calls'])
            self.assertIsNotNone(f['peak_resident_bytes'])
        result = subprocess.run([sys.executable, str(ROOT/'tools/profile_report.py'), str(path),
                                 '--runs', '1', '--warmup', '0', '--duration', '0'], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr or result.stdout)

    def test_native_profile_and_state(self):
        if not NATIVE:
            self.skipTest('pass --native on a graphical test host')
        path = self.root/'native.jsonl'
        replay = ROOT/'examples/lantern/replays/tour.txt'
        captured = self.root/'profiled.png'
        plain_capture = self.root/'plain.png'
        native = self.run_game('--frames', 30, '--replay', replay, '--profile', path, '--capture', captured)
        plain = self.run_game('--frames', 30, '--replay', replay, '--capture', plain_capture)
        self.assertEqual(native, plain)
        self.assertEqual(captured.read_bytes(), plain_capture.read_bytes(), 'profiling changed native pixels')
        headless = self.run_game('--frames', 30, '--replay', replay, '--headless')
        for field in ('tick', 'hash', 'entities', 'projectiles', 'state', 'watches'):
            self.assertEqual(native[field], headless[field], field)
        header, frames, gpu, end = report.load(path)
        self.assertTrue(header['graphics'])
        self.assertGreater(sum(f['wait_ms'] for f in frames), 10)
        if header['gpu_timer'] == 'gl_time_elapsed':
            self.assertGreater(len(gpu), 0)
            self.assertGreater(max(gpu.values()), 0)
            self.assertEqual(len(gpu)+end['gpu_pending'], sum(f['gpu_issued'] for f in frames))
        else:
            self.assertFalse(gpu)
        print(f'native GPU timer: {header["gpu_timer"]}, samples={len(gpu)}, pending={end["gpu_pending"]}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('binary', type=Path, nargs='?')
    parser.add_argument('--native', action='store_true')
    args = parser.parse_args()
    BINARY = args.binary.resolve() if args.binary else None
    NATIVE = args.native
    unittest.main(argv=[sys.argv[0]])
