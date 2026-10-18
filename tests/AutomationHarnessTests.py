"""Run with py -3.13 tests/AutomationHarnessTests.py; no controller or game launch."""
import argparse
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from MenuDrive import parse_actions
from FightMetrics import summarize
from GpuLossReport import summarize as summarize_gpu_loss
from TracyZones import aggregate, load, report
import RunnerJob as runner_job
import FightWatch as fight_watch
spec = importlib.util.spec_from_file_location('loop_supervisor', ROOT / 'Run-AutonomousLoop.py')
supervisor = importlib.util.module_from_spec(spec)
spec.loader.exec_module(supervisor)


class AutomationTests(unittest.TestCase):
    def test_fight_watch_publication_retries_windows_reader_lock(self):
        with tempfile.TemporaryDirectory(dir=ROOT / '_Build') as directory:
            folder = Path(directory)
            target = folder / 'watch.json'
            target.write_text('{"action": "continue"}')
            replace = Path.replace
            attempts = []

            def locked_then_replace(source, destination):
                attempts.append(source)
                if len(attempts) <= 2:
                    self.assertEqual(json.loads(target.read_text())['action'], 'continue')
                    raise PermissionError('Windows reader holds the destination')
                return replace(source, destination)

            with mock.patch.object(Path, 'replace', locked_then_replace), \
                    mock.patch.object(fight_watch.time, 'sleep'):
                self.assertTrue(fight_watch.publish(folder, {'action': 'stop', 'reason': 'fatal'}))
            self.assertEqual(len(attempts), 3)
            self.assertEqual(json.loads(target.read_text()), {'action': 'stop', 'reason': 'fatal'})

    def test_fight_watch_busy_publication_preserves_state_for_next_poll(self):
        with tempfile.TemporaryDirectory(dir=ROOT / '_Build') as directory:
            folder = Path(directory)
            target = folder / 'watch.json'
            target.write_text('{"action": "continue"}')
            state = {'action': 'stop', 'reason': 'fatal'}
            with mock.patch.object(Path, 'replace', side_effect=PermissionError('busy')), \
                    mock.patch.object(fight_watch.time, 'sleep'):
                self.assertFalse(fight_watch.publish(folder, state))
            self.assertEqual(json.loads(target.read_text())['action'], 'continue')
            self.assertTrue(fight_watch.publish(folder, state))
            self.assertEqual(json.loads(target.read_text()), state)

    def test_runner_media_options_reach_worker_arguments(self):
        args = argparse.Namespace(action='fight', label='d094-load-media-diag', filter='',
                                  monitor_seconds=420, tracy_seconds=60, tracy_start_seconds=45,
                                  screenshot_every_sec=1, shader_dump=True)
        spec = runner_job.make_spec(args)
        self.assertEqual(spec['tracy_start_seconds'], 45)
        self.assertEqual(spec['screenshot_every_sec'], 1)
        self.assertTrue(spec['shader_dump'])
        script = r'''$ErrorActionPreference = 'Stop'; $tokens = $null; $errors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile('__WORKER__', [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw 'Worker syntax error' }
$fn = $ast.Find({param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Get-FightArguments'}, $true)
if (-not $fn) { throw 'Fight argument function missing' }
Invoke-Expression $fn.Extent.Text
$spec = ConvertFrom-Json '__SPEC__'
ConvertTo-Json -Compress -InputObject @(Get-FightArguments $spec '__ROOT__' 'C:\game files')
'''
        script = script.replace('__WORKER__', str(ROOT / '_Build' / 'claude-runner' /
                                                  'Claude-JobRunner.ps1').replace("'", "''"))
        script = script.replace('__SPEC__', json.dumps(spec).replace("'", "''"))
        script = script.replace('__ROOT__', str(ROOT).replace("'", "''"))
        result = subprocess.run(['powershell.exe', '-NoProfile', '-NonInteractive',
                                 '-Command', script], capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        arguments = json.loads(result.stdout)
        self.assertEqual(arguments[arguments.index('-TracyStartSeconds') + 1], 45)
        self.assertEqual(arguments[arguments.index('-ScreenshotEverySec') + 1], 1)
        self.assertEqual(arguments[arguments.index('-MonitorSeconds') + 1], 420)
        self.assertIn('-ShaderDump', arguments)
        self.assertEqual(arguments[arguments.index('-GameDir') + 1], '"C:\\game files"')
        args.tracy_start_seconds = -1
        with self.assertRaises(ValueError):
            runner_job.make_spec(args)
        args.tracy_start_seconds = None
        args.screenshot_every_sec = 0
        with self.assertRaises(ValueError):
            runner_job.make_spec(args)

    def test_status_only_work_is_not_progress_and_selects_fallback(self):
        with tempfile.TemporaryDirectory(dir=ROOT / '_Build') as directory:
            root = Path(directory)
            (root / 'src').mkdir()
            engine = root / 'src' / 'engine.cpp'
            engine.write_text('original')
            done = root / '_Build' / 'claude-runner' / 'done'
            done.mkdir(parents=True)
            with mock.patch.object(supervisor, 'ROOT', root):
                baseline = supervisor.work_snapshot()
                (root / 'HANDOFF.md').write_text('same blocker again')
                (done / 'gitstatus.json').write_text(json.dumps({'action': 'gitstatus'}))
                (done / 'gitstatus.exit').write_text('0')
                self.assertEqual(baseline, supervisor.work_snapshot())
                state = {'cooldown': {}}
                sol, flash = supervisor.MODELS
                self.assertFalse(supervisor.note_progress(state, sol, baseline, baseline, 1000))
                self.assertTrue(supervisor.note_progress(state, sol, baseline, baseline, 1001))
                self.assertEqual(supervisor.choose_model(state['cooldown'], 1002), flash)
                (done / 'fight.json').write_text(json.dumps({'action': 'fight'}))
                (done / 'fight.exit').write_text('0')
                after = supervisor.work_snapshot()
                self.assertNotEqual(baseline, after)
                self.assertFalse(supervisor.note_progress(state, flash, baseline, after, 1003))
                self.assertEqual(state['no_progress_cycles'], 0)
                engine.write_text('changed engine content')
                self.assertNotEqual(after, supervisor.work_snapshot())

    @unittest.skipUnless(sys.platform == 'win32', 'Windows runner recovery')
    def test_runner_recovery_starts_absent_worker_but_leaves_live_worker_and_game(self):
        with tempfile.TemporaryDirectory(dir=ROOT / '_Build') as directory:
            transfer = Path(directory)
            root = transfer / 'KytyPS5-UFC'
            (root / '_Build' / 'windows').mkdir(parents=True)
            (root / '_Build' / 'windows' / 'CMakeCache.txt').write_text('configured')
            (transfer / 'Start-Claude-Worker.cmd').write_text('placeholder')
            with mock.patch.object(supervisor, 'ROOT', root), \
                    mock.patch.object(supervisor.time, 'time', return_value=1000), \
                    mock.patch.object(supervisor, 'process_snapshot', return_value=[]) as snapshot, \
                    mock.patch.object(supervisor.subprocess, 'Popen') as launch:
                launch.return_value.pid = 9876
                state = {}
                supervisor.recover_runner(state)
                launch.assert_called_once()
                self.assertEqual(state['runner_console_pid'], 9876)
                supervisor.recover_runner(state)
                launch.assert_called_once()  # Retry cooldown prevents duplicate startup attempts.
                launch.reset_mock()
                snapshot.return_value = [dict(Name='powershell.exe', CommandLine=
                    'powershell -File "C:\\transfer\\Start-Transfer.ps1" -Mode Claude')]
                supervisor.recover_runner({})
                launch.assert_not_called()
                snapshot.return_value = [dict(Name='kyty_emulator.exe')]
                supervisor.recover_runner({})
                launch.assert_not_called()

    @unittest.skipUnless(sys.platform == 'win32', 'Windows power request')
    def test_awake_request_is_process_scoped_and_released(self):
        with mock.patch('ctypes.windll.kernel32.SetThreadExecutionState', return_value=1) as call:
            supervisor.set_awake(True)
            supervisor.set_awake(False)
            self.assertEqual(call.call_args_list, [mock.call(0x80000001), mock.call(0x80000000)])

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

    def test_fight_frame_rate_uses_title_frame_counter(self):
        rows = [dict(elapsed_s=t, fps=1, frame=frame) for t, frame in
                [(5, 100), (10, 200), (40, ''), (20, 450), (70, 1400), (60, 'ERR')]]
        result = summarize(rows, dict(fight_detected=True, first_fight_s=10))
        self.assertEqual(result['fight_frames'], 1200)
        self.assertEqual(result['fight_frame_rate'], 20)
        result = summarize([dict(elapsed_s=10, frame=5)], dict(fight_detected=True, first_fight_s=10))
        self.assertEqual(result['fight_frames'], 'n/a')
        self.assertEqual(result['fight_frame_rate'], 'n/a')

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
        sol, flash = supervisor.MODELS
        self.assertEqual(flash, 'openrouter/deepseek/deepseek-v4.1-flash')
        self.assertIn('--variant high', ' '.join(supervisor.command(sol)))
        self.assertNotIn('--variant', supervisor.command(flash))
        now = 1000
        cooldown = {sol: now + 5 * 3600}
        self.assertEqual(supervisor.choose_model(cooldown, now), flash)
        cooldown[flash] = now + 5 * 3600
        self.assertIsNone(supervisor.choose_model(cooldown, now))
        self.assertEqual(supervisor.choose_model(cooldown, now + 5 * 3600), sol)
        ordinary = json.dumps({'type': 'text', 'text': 'quota retry is configured'})
        self.assertEqual(supervisor.error_text(ordinary), '')
        error = json.dumps({'type': 'error', 'error': {'message': 'usage limit reached'}})
        self.assertTrue(supervisor.QUOTA.search(supervisor.error_text(error)))
        openrouter_error = json.dumps({'type': 'error', 'error': {
            'statusCode': 402, 'message': 'Insufficient credits'}})
        self.assertTrue(supervisor.QUOTA.search(supervisor.error_text(openrouter_error)))
        environment = supervisor.child_environment(ROOT / '_Build' / 'agent-loop')
        self.assertNotIn('OPENCODE_SERVER_PASSWORD', environment)
        self.assertNotIn('OPENCODE_SERVER_USERNAME', environment)
        self.assertTrue(environment['OPENCODE_DB'].endswith('opencode-loop.db'))

    def test_cooldown_wait_ignores_retired_fallbacks(self):
        with tempfile.TemporaryDirectory(dir=ROOT / '_Build') as directory:
            root = Path(directory)
            folder = root / '_Build' / 'agent-loop'
            folder.mkdir(parents=True)
            cooldown = dict.fromkeys(supervisor.MODELS, 1200)
            cooldown['meta/muse-spark-1.3'] = 999
            (folder / 'state.json').write_text(json.dumps({'cycles': 0, 'cooldown': cooldown}))

            def stop_after_wait(seconds):
                self.assertEqual(seconds, 60)
                state = json.loads((folder / 'state.json').read_text())
                self.assertEqual(state['status'], 'waiting for quota cooldown')
                self.assertEqual(state['model_priority'], list(supervisor.MODELS))
                self.assertEqual(state['cycles'], 0)
                (folder / 'STOP').touch()

            with mock.patch.object(supervisor, 'ROOT', root), \
                    mock.patch.object(sys, 'argv', ['Run-AutonomousLoop.py', '--max-cycles', '1']), \
                    mock.patch.object(supervisor, 'preflight_blockers', return_value=[]), \
                    mock.patch.object(supervisor.time, 'time', return_value=1000), \
                    mock.patch.object(supervisor.time, 'sleep', side_effect=stop_after_wait), \
                    mock.patch.object(supervisor.subprocess, 'Popen') as launch:
                supervisor.main()
                launch.assert_not_called()

    @unittest.skipUnless(sys.platform == 'win32', 'Windows process pre-flight')
    def test_human_input_wait_does_not_spend_cycles_and_clears_on_resume(self):
        with tempfile.TemporaryDirectory(dir=ROOT / '_Build') as directory:
            root = Path(directory)
            folder = root / '_Build' / 'agent-loop'
            folder.mkdir(parents=True)
            heartbeat = root / '_Build' / 'claude-runner' / 'heartbeat.txt'
            heartbeat.parent.mkdir(parents=True)
            heartbeat.write_text('runner heartbeat')
            request = folder / 'WAIT-FOR-INPUT.json'
            request.write_text(json.dumps({'reason': 'Need blackout timing', 'questions': []}))
            snapshot = subprocess.CompletedProcess([], 0, stdout='[]', stderr='')

            def stop_after_wait(seconds):
                self.assertEqual(seconds, 60)
                state = json.loads((folder / 'state.json').read_text())
                self.assertEqual(state['status'], 'waiting for user input')
                self.assertEqual(state['cycles'], 0)
                self.assertEqual(state['blockers'], ['Human input required: Need blackout timing'])
                (folder / 'STOP').touch()

            with mock.patch.object(supervisor, 'ROOT', root), \
                    mock.patch.object(sys, 'argv', ['Run-AutonomousLoop.py', '--max-cycles', '1']), \
                    mock.patch.object(supervisor.subprocess, 'run', return_value=snapshot), \
                    mock.patch.object(supervisor.time, 'sleep', side_effect=stop_after_wait), \
                    mock.patch.object(supervisor.subprocess, 'Popen') as launch:
                self.assertEqual(supervisor.preflight_blockers(),
                                 ['Human input required: Need blackout timing'])
                request.write_text('incomplete JSON')
                self.assertIn('cannot read', supervisor.preflight_blockers()[0])
                request.write_text(json.dumps({'reason': 'Need blackout timing'}))
                supervisor.main()
                launch.assert_not_called()
                request.unlink()
                self.assertEqual(supervisor.preflight_blockers(), [])

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

    def test_preflight_blocks_games_builds_tests_and_competing_runners(self):
        processes = [
            dict(Name='launcher.exe', ProcessId=1),
            dict(Name='kyty_emulator.exe', ProcessId=2),
            dict(Name='ctest.exe', ProcessId=3),
            dict(Name='shader_cfg_tests.exe', ProcessId=4),
            dict(Name='ninja.exe', ProcessId=5),
            dict(Name='powershell.exe', ProcessId=6,
                 CommandLine='powershell -File E:\\dev\\KytyPS5-UFC\\_Build\\claude-runner\\Claude-JobRunner.ps1'),
            dict(Name='python.exe', ProcessId=7, CommandLine='python MenuDrive.py'),
            dict(Name='opencode-cli.exe', ProcessId=8, CommandLine='opencode run --agent ufc-loop'),
        ]
        self.assertEqual(len(supervisor.process_blockers(processes)), len(processes) - 1)
        self.assertNotIn('powershell.exe PID 6', supervisor.process_blockers(processes))
        idle = [dict(Name='OpenCode.exe', CommandLine='OpenCode.exe --type=renderer'),
                dict(Name='opencode-cli.exe', CommandLine='opencode serve'),
                dict(Name='python.exe', CommandLine='python tests/AutomationHarnessTests.py')]
        self.assertEqual(supervisor.process_blockers(idle), [])

    @unittest.skipUnless(sys.platform == 'win32', 'Windows process pre-flight')
    def test_preflight_checks_heartbeat_and_fails_closed_on_process_query_errors(self):
        with tempfile.TemporaryDirectory(dir=ROOT / '_Build') as directory:
            root = Path(directory)
            heartbeat = root / '_Build' / 'claude-runner' / 'heartbeat.txt'
            heartbeat.parent.mkdir(parents=True)
            heartbeat.write_text('runner heartbeat')
            snapshot = subprocess.CompletedProcess([], 0, stdout='[]', stderr='')
            with mock.patch.object(supervisor, 'ROOT', root), \
                    mock.patch.object(supervisor.subprocess, 'run', return_value=snapshot) as query:
                self.assertEqual(supervisor.preflight_blockers(), [])
                with mock.patch.object(supervisor.time, 'time', return_value=time.time() + 121):
                    self.assertTrue(any('heartbeat' in b for b in supervisor.preflight_blockers()))
                heartbeat.unlink()
                self.assertTrue(any('has never run' in b for b in supervisor.preflight_blockers()))
                query.side_effect = subprocess.TimeoutExpired('powershell', 30)
                self.assertTrue(any('Cannot verify' in b for b in supervisor.preflight_blockers()))
                query.side_effect = None
                query.return_value.stdout = '{}'
                self.assertTrue(any('Cannot verify' in b for b in supervisor.preflight_blockers()))

    def test_blocked_cycle_does_not_launch_an_agent_or_spend_a_cycle(self):
        with tempfile.TemporaryDirectory(dir=ROOT / '_Build') as directory:
            root = Path(directory)
            folder = root / '_Build' / 'agent-loop'
            def stop_after_wait(seconds):
                self.assertEqual(seconds, 60)
                state = json.loads((folder / 'state.json').read_text())
                self.assertEqual(state['status'], 'waiting for pre-flight')
                self.assertEqual(state['cycles'], 0)
                self.assertEqual(state['cooldown'], {})
                (folder / 'STOP').touch()
            with mock.patch.object(supervisor, 'ROOT', root), \
                    mock.patch.object(sys, 'argv', ['Run-AutonomousLoop.py', '--max-cycles', '1']), \
                    mock.patch.object(supervisor, 'preflight_blockers', return_value=['manual play']), \
                    mock.patch.object(supervisor.time, 'sleep', side_effect=stop_after_wait), \
                    mock.patch.object(supervisor.subprocess, 'Popen') as launch:
                supervisor.main()
                launch.assert_not_called()
                self.assertEqual(json.loads((folder / 'state.json').read_text())['status'], 'stopped')

    @unittest.skipUnless(sys.platform == 'win32', 'Windows process pre-flight')
    def test_preflight_ignores_own_calling_wrappers_but_blocks_foreign_automation(self):
        with tempfile.TemporaryDirectory(dir=ROOT / '_Build') as directory:
            heartbeat = Path(directory) / '_Build' / 'claude-runner' / 'heartbeat.txt'
            heartbeat.parent.mkdir(parents=True)
            heartbeat.write_text('runner heartbeat')
            processes = [dict(Name='python.exe', ProcessId=10, ParentProcessId=20),
                         dict(Name='powershell.exe', ProcessId=20, ParentProcessId=0,
                              CommandLine='py Run-AutonomousLoop.py --preflight-only; powershell -File AutoFight.ps1')]
            result = subprocess.CompletedProcess([], 0, stdout=json.dumps(processes), stderr='')
            with mock.patch.object(supervisor, 'ROOT', Path(directory)), \
                    mock.patch.object(supervisor.os, 'getpid', return_value=10), \
                    mock.patch.object(supervisor.subprocess, 'run', return_value=result):
                self.assertEqual(supervisor.preflight_blockers(), [])
                processes.append(dict(Name='powershell.exe', ProcessId=30, ParentProcessId=0,
                                      CommandLine='powershell -File AutoFight.ps1'))
                result.stdout = json.dumps(processes)
                self.assertEqual(supervisor.preflight_blockers(), ['powershell.exe PID 30'])
                processes[1] = dict(Name='kyty_emulator.exe', ProcessId=20, ParentProcessId=0)
                result.stdout = json.dumps(processes)
                self.assertIn('kyty_emulator.exe PID 20', supervisor.preflight_blockers())

    @unittest.skipUnless(sys.platform == 'win32', 'PowerShell build guard')
    def test_build_guard_refuses_active_play_without_stopping_processes(self):
        # Invoke only the parsed guard definition, never the build script's body.
        script = r'''$tokens = $null; $errors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile($args[0], [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw 'Build script syntax error' }
$guard = $ast.Find({ param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Assert-GameClosed' }, $true)
if (-not $guard) { throw 'Build guard missing' }
Invoke-Expression $guard.Extent.Text
function Stop-Process { throw 'Guard attempted to stop a process' }
function Get-Process { [CmdletBinding()] param([string[]]$Name) }
Assert-GameClosed
function Get-Process { [CmdletBinding()] param([string[]]$Name) [pscustomobject]@{ ProcessName = 'launcher'; Id = 123 } }
try { Assert-GameClosed; throw 'Guard accepted active play' } catch {
    if ($_.Exception.Message -notlike 'Launcher/emulator is active.*') { throw }
}
'''
        # PowerShell -Command does not populate $args like -File; use a literal path.
        script = script.replace('$args[0]', "'" + str(ROOT / 'Build-Windows.ps1').replace("'", "''") + "'")
        script += '\nexit 0\n'
        result = subprocess.run(['powershell.exe', '-NoProfile', '-NonInteractive',
                                 '-Command', "$ErrorActionPreference = 'Stop'; " + script],
                                capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

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
