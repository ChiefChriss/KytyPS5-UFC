"""Lightweight checker regressions; no model, game, build or scheduled-task launch."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('progress_check', ROOT / 'Check-UFC-Progress.py')
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)


class ProgressTests(unittest.TestCase):
    def test_large_snapshot_is_attached_and_cannot_expand_windows_command_line(self):
        review = dict(status='on_track', summary='Verification underway.',
                      evidence=['Build completed'], next_action='Complete regression checks.')
        captured = {}

        def fake_launch(command, **kwargs):
            captured.update(command=command, environment=kwargs['env'])
            kwargs['stdout'].write(json.dumps(dict(type='text', part=dict(text=json.dumps(review)))) + '\n')
            child = mock.Mock()
            child.wait.return_value = 0
            return child

        with tempfile.TemporaryDirectory(dir=ROOT / '_Build') as directory:
            folder = Path(directory)
            with mock.patch.object(checker, 'FOLDER', folder), \
                    mock.patch.object(checker.subprocess, 'Popen', fake_launch):
                self.assertEqual(checker.review_with_model({'evidence': 'x' * 40000}, 'check', 30), review)
            command = captured['command']
            self.assertLess(len(' '.join(command)), 4096)
            self.assertEqual(command[command.index('--model') + 1], checker.MODEL)
            self.assertTrue(checker.MODEL.endswith(':free'))
            attachment = Path(command[command.index('--file') + 1])
            self.assertEqual(len(json.loads(attachment.read_text())['evidence']), 40000)
            config = json.loads(captured['environment']['OPENCODE_CONFIG_CONTENT'])
            self.assertEqual(config['small_model'], checker.MODEL)
            self.assertFalse(config['snapshot'])

    def test_busy_worker_and_live_cycle_do_not_depend_on_heartbeat_age(self):
        items = [dict(ProcessId=1, Name='python.exe', CommandLine='Run-AutonomousLoop.py'),
                 dict(ProcessId=2, Name='opencode.exe', CommandLine='opencode run --agent ufc-loop'),
                 dict(ProcessId=3, Name='powershell.exe', CommandLine='Start-Transfer.ps1 -Mode Claude'),
                 dict(ProcessId=4, Name='kyty_emulator.exe', CommandLine='game')]
        result = checker.roles(items, 1, 2)
        self.assertTrue(result['supervisor_alive'])
        self.assertTrue(result['lead_agent_alive'])
        self.assertTrue(result['worker_alive'])
        self.assertEqual(result['active_work'], [{'name': 'kyty_emulator.exe', 'pid': 4}])
        self.assertFalse(checker.roles([], 1, 2)['supervisor_alive'])

    def test_provider_error_is_not_a_successful_review(self):
        with self.assertRaisesRegex(ValueError, 'Provider error'):
            checker.parse_review(json.dumps({'type': 'error', 'error': 'quota'}))

    def test_review_schema_and_fragmented_response(self):
        review = dict(status='attention', summary='Visual failure captured.',
                      evidence=['18.54 FPS with corruption'], next_action='Verify synchronized readback.')
        answer = json.dumps(review)
        events = '\n'.join(json.dumps(dict(type='text', part=dict(text=part)))
                           for part in (answer[:30], answer[30:]))
        self.assertEqual(checker.parse_review(events), review)
        review['status'] = '20 FPS achieved'
        with self.assertRaisesRegex(ValueError, 'invalid status'):
            checker.parse_review(json.dumps(dict(type='text', part=dict(text=json.dumps(review)))))


if __name__ == '__main__':
    unittest.main()
