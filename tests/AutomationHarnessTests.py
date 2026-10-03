"""Run with py -3.13 tests/AutomationHarnessTests.py; no controller or game launch."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from MenuDrive import parse_actions
from FightMetrics import summarize
from GpuLossReport import summarize as summarize_gpu_loss
from TracyZones import aggregate, load, report
spec = importlib.util.spec_from_file_location('loop_supervisor', ROOT / 'Run-AutonomousLoop.py')
supervisor = importlib.util.module_from_spec(spec)
spec.loader.exec_module(supervisor)


class AutomationTests(unittest.TestCase):
    def test_tracy_combines_threads_with_weighted_mean_and_separate_sources(self):
        def row(total, count, line='10'):
            return dict(name='zone', src_file='src\\render.cpp', src_line=line,
                        total_ns=str(total), counts=str(count), mean_ns='999999')
        current = aggregate([row(1000, 1), row(9000, 3), row(0, 0, '11')])
        self.assertEqual(current[('zone', 'src/render.cpp', '10')], [10000, 4])
        text = report(current, aggregate([row(1000, 2)]))
        self.assertIn('| 4 | 0.010 | 2.500 | 0.500 | 5.000 |', text)
        self.assertIn('| 0 | 0.000 | n/a | n/a | n/a |', text)
        for invalid in (row(-1, 1), row(1, 0), row(1, -1)):
            with self.assertRaises(ValueError):
                aggregate([invalid])

    def test_tracy_reads_powershell_bom_and_quoted_csv(self):
        with tempfile.TemporaryDirectory(dir=ROOT / '_Build') as directory:
            path = Path(directory) / 'zones.csv'
            path.write_text('name,src_file,src_line,total_ns,counts\n'
                            '"zone, quoted",src/a.cpp,7,2000,2\n', encoding='utf-8-sig')
            self.assertEqual(load(path), {('zone, quoted', 'src/a.cpp', '7'): [2000, 2]})

    def test_fight_metrics_exclude_title_and_keep_zero_fps(self):
        rows = [dict(elapsed_s=t, fps=fps, proc_rss_mb=rss, gpu_mem_mb=vram)
                for t, fps, rss, vram in [(9, 60, 9999, 9999), (10, 0, 100, ''),
                                          (70, 12, 200, 300), (130, '', 150, 400)]]
        result = summarize(rows, dict(fight_detected=True, first_fight_s=10))
        self.assertEqual(result['fight_median_fps'], 6)
        self.assertEqual(result['fight_fps_samples'], 2)
        self.assertEqual(result['fight_zero_fps_samples'], 1)
        self.assertEqual(result['fight_max_rss_mb'], 200)
        self.assertEqual(result['fight_max_device_vram_mb'], 400)
        self.assertTrue(result['fight_120s_observed'])
        self.assertEqual(result['fight_observed_s'], 120)
        self.assertEqual(result['fight_max_sample_gap_s'], 60)

    def test_fight_metrics_report_initial_gap_and_sort_timestamps(self):
        result = summarize([dict(elapsed_s=25, fps=10), dict(elapsed_s=20, fps=12)],
                           dict(fight_detected=True, first_fight_s=10))
        self.assertEqual(result['fight_max_sample_gap_s'], 10)
        self.assertEqual(result['fight_sample_span_s'], 5)
        self.assertEqual(result['fight_median_fps'], 11)

    def test_fight_metrics_no_detection_and_invalid_values(self):
        rows = [dict(elapsed_s=50, fps='nan', proc_rss_mb='ERR', gpu_mem_mb='inf')]
        for watch in ({}, dict(fight_detected=False, first_fight_s=0),
                      dict(fight_detected=True, first_fight_s=None)):
            result = summarize(rows, watch)
            self.assertEqual(result['fight_samples'], 0)
            self.assertEqual(result['fight_median_fps'], 'n/a')
            self.assertFalse(result['fight_120s_observed'])
        result = summarize(rows, dict(fight_detected=True, first_fight_s=0))
        self.assertEqual(result['fight_fps_samples'], 0)
        self.assertEqual(result['fight_max_device_vram_mb'], 'n/a')

    def test_fight_cpu_slope_uses_only_valid_consecutive_fight_samples(self):
        rows = [dict(elapsed_s=t, proc_cpu_s=cpu) for t, cpu in
                [(9, 0), (10, 100), (12, 104), (14, 110), (16, ''),
                 (18, 200), (20, 1), (20, 2), (22, 'nan')]]
        result = summarize(list(reversed(rows)), dict(fight_detected=True, first_fight_s=10))
        self.assertEqual(result['fight_cpu_intervals'], 2)
        self.assertEqual(result['fight_median_cpu_busy_cores'], 2.5)
        result = summarize([dict(elapsed_s=10, proc_cpu_s=100)],
                           dict(fight_detected=True, first_fight_s=10))
        self.assertEqual(result['fight_cpu_intervals'], 0)
        self.assertEqual(result['fight_median_cpu_busy_cores'], 'n/a')

    def test_quota_priority_returns_to_sol_after_five_hours(self):
        sol, paid, free = supervisor.MODELS
        self.assertIn('--variant high', ' '.join(supervisor.command(sol)))
        now = 1000
        cooldown = {sol: now + 5 * 3600}
        self.assertEqual(supervisor.choose_model(cooldown, now), paid)
        cooldown[paid] = now + 5 * 3600
        self.assertEqual(supervisor.choose_model(cooldown, now), free)
        self.assertEqual(supervisor.choose_model(cooldown, now + 5 * 3600), sol)
        ordinary = json.dumps({'type': 'text', 'text': 'quota retry is configured'})
        self.assertEqual(supervisor.error_text(ordinary), '')
        error = json.dumps({'type': 'error', 'error': {'message': 'usage limit reached'}})
        self.assertTrue(supervisor.QUOTA.search(supervisor.error_text(error)))
        environment = supervisor.child_environment(ROOT / '_Build' / 'agent-loop')
        self.assertNotIn('OPENCODE_SERVER_PASSWORD', environment)
        self.assertNotIn('OPENCODE_SERVER_USERNAME', environment)
        self.assertTrue(environment['OPENCODE_DB'].endswith('opencode-loop.db'))

    def test_gpu_loss_timeout_with_breadcrumb_suspects(self):
        suspect = ('GPU breadcrumb suspect: seq={} op={} submit=7 args=1,1,1,0,0x0000000000000000 '
                   'ps=0x0000001140f04000 es=0x0000000000000000 gs=0x0000000000000000 '
                   'shader_hash={}')
        lines = ['GPU breadcrumbs: recorded=900 gpu_started=800 gpu_completed=500',
                 'vkQueueSubmit failed: ErrorDeviceLost (-4), tick=1 debug_op=3',
                 'GPU breadcrumbs: recorded=1000 gpu_started=12 gpu_completed=10 suspects=3',
                 suspect.format(10, 'DrawIndex', '0000000000000000'),
                 suspect.format(11, 'DispatchDirect', 'af3f147d3e75c30e'),
                 suspect.format(12, 'DrawIndex', '0000000000000000'),
                 'GPU device fault: "fault" addresses=1 vendor_infos=0',
                 '  address type=InstructionPointerInvalid reported=0x2 precision=0x1']
        result = summarize_gpu_loss(lines, [dict(provider='nvlddmkm', id='153')])
        self.assertTrue(result['gpu_device_lost'])
        self.assertEqual(result['gpu_loss_class'], 'timeout')
        self.assertEqual(result['gpu_driver_events'], 'nvlddmkm:153')
        self.assertEqual(result['gpu_breadcrumb_range'], '10-12 of 1000')
        self.assertEqual(result['gpu_breadcrumb_suspects'], '3')
        self.assertEqual(result['gpu_breadcrumb_ops'], 'DrawIndexx2,DispatchDirectx1')
        self.assertEqual(result['gpu_breadcrumb_compute_hashes'], 'af3f147d3e75c30e')
        self.assertEqual(result['gpu_fault_address_types'], 'InstructionPointerInvalid')

    def test_gpu_loss_memory_fault_and_clean_runs(self):
        fault = ['vkQueueSubmit failed: ErrorDeviceLost (-4)', '  address type=ReadInvalid reported=0x1']
        self.assertEqual(summarize_gpu_loss(fault, [])['gpu_loss_class'], 'memory_fault')
        self.assertEqual(summarize_gpu_loss(fault[:1], [])['gpu_loss_class'], 'unknown')
        self.assertEqual(summarize_gpu_loss(fault[:1], [dict(provider='nvlddmkm', id='14')])
                         ['gpu_loss_class'], 'driver_error')
        clean = summarize_gpu_loss(['GPU breadcrumbs: recorded=5 gpu_started=5 gpu_completed=5'], [])
        self.assertEqual(clean['gpu_loss_class'], 'none')
        self.assertEqual(clean['gpu_breadcrumb_range'], 'n/a')
        self.assertEqual(summarize_gpu_loss([], [dict(provider='Display', id='4101')])
                         ['gpu_loss_class'], 'driver_event_without_loss')

    def test_short_presses_then_long_holds_do_not_overlap(self):
        actions = parse_actions('9:1x0,1.5:3x1.5,3:8x1.5,1.5:30x1.5,1.5:12x5.5@4')
        self.assertEqual(len(actions), 54)
        self.assertEqual(actions[41], (72, 0.25))
        self.assertEqual(actions[42], (73.5, 4))
        self.assertEqual(actions[-1], (134, 4))
        self.assertTrue(all(next_time >= at + hold for (at, hold), (next_time, _) in zip(actions, actions[1:])))
        for invalid in ('0:12x1.5@4', 'nan:1x0', '0:1x0@-1', '0:0x1.5'):
            with self.assertRaises(SystemExit):
                parse_actions(invalid)

    def test_watcher_ignores_nonfatal_noise_then_latches_fragmented_fatal(self):
        with tempfile.TemporaryDirectory(dir=ROOT / '_Build') as directory:
            session = Path(directory)
            log = session / 'console.log'
            log.write_bytes(b'Controller connected\nUnresolved import stub called\n')
            worker = subprocess.Popen([sys.executable, str(ROOT / 'FightWatch.py'),
                                       '--session', str(session), '--start-epoch', str(time.time())],
                                      stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            try:
                deadline = time.monotonic() + 10
                while not (session / 'watch.json').exists() and time.monotonic() < deadline:
                    time.sleep(0.1)
                state = json.loads((session / 'watch.json').read_text())
                self.assertEqual(state['action'], 'continue')
                with log.open('ab') as stream:
                    stream.write(b'vkQueueSubmit failed: ErrorDev')
                time.sleep(1.2)
                with log.open('ab') as stream:
                    stream.write(b'iceLost (-4)\n')
                worker.communicate(timeout=10)
                self.assertEqual(worker.returncode, 0)
                state = json.loads((session / 'watch.json').read_text())
                self.assertEqual(state['action'], 'stop')
                self.assertIn('ErrorDeviceLost', state['reason'])
                self.assertTrue((session / 'correction-request.md').exists())
            finally:
                if worker.poll() is None:
                    worker.kill()
                    worker.communicate()


if __name__ == '__main__':
    unittest.main()
