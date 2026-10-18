"""One read-only, free-model progress check. Scheduled every five minutes on Windows.

Only this wrapper writes _Build/progress-watch evidence/reports. It never changes engine
files or queues worker jobs. An OS lock and scheduler IgnoreNew prevent overlapping checks.
"""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parent
MODEL = 'openrouter/nvidia/nemotron-3.5-lightning:free'
FOLDER = ROOT / '_Build' / 'progress-watch'
CLI = ROOT.parent / 'Tools' / 'opencode-progress-cli' / 'node_modules' / 'opencode-windows-x64' / 'bin' / 'opencode.exe'


def stamp():
    return datetime.now(timezone.utc).isoformat()


def text(path, limit=6000, tail=False):
    try:
        data = path.read_bytes()
        for encoding in ('utf-8-sig', 'utf-16'):
            try:
                result = data.decode(encoding)
                break
            except UnicodeDecodeError:
                continue
        else:
            result = data.decode('utf-8', errors='replace')
        return result[-limit:] if tail else result[:limit]
    except OSError as error:
        return f'Unavailable: {error}'


def load(path, default=None):
    try:
        return json.loads(path.read_text(encoding='utf-8-sig'))
    except (OSError, ValueError):
        return default


def age(path):
    try:
        return round(max(0, time.time() - path.stat().st_mtime), 1)
    except OSError:
        return None


def atomic_write(path, content):
    temporary = path.with_suffix(path.suffix + '.tmp')
    temporary.write_text(content, encoding='utf-8')
    for attempt in range(20):
        try:
            temporary.replace(path)
            return
        except PermissionError:
            if attempt == 19:
                raise
            time.sleep(0.05)


def processes():
    script = ("$ErrorActionPreference='Stop'; [Console]::OutputEncoding=[System.Text.UTF8Encoding]::new(); "
              "ConvertTo-Json -Compress -InputObject @(Get-CimInstance Win32_Process | "
              "Select-Object Name,ProcessId,ParentProcessId,CommandLine)")
    result = subprocess.run(['powershell.exe', '-NoProfile', '-NonInteractive', '-Command', script],
                            capture_output=True, text=True, encoding='utf-8', timeout=25, check=True)
    return json.loads(result.stdout)


def roles(items, supervisor_pid, agent_pid):
    names = ('kyty_emulator.exe', 'launcher.exe', 'ctest.exe', 'cmake.exe', 'ninja.exe',
             'clang-cl.exe', 'lld-link.exe')
    return {
        'supervisor_alive': any(p.get('ProcessId') == supervisor_pid for p in items),
        'lead_agent_alive': any(p.get('ProcessId') == agent_pid for p in items),
        'worker_alive': any('Start-Transfer.ps1' in (p.get('CommandLine') or '') and
                            '-Mode Claude' in (p.get('CommandLine') or '') or
                            'Claude-JobRunner.ps1' in (p.get('CommandLine') or '') for p in items),
        'active_work': [{'name': p.get('Name'), 'pid': p.get('ProcessId')}
                        for p in items if (p.get('Name') or '').lower() in names or
                        (p.get('Name') or '').lower().endswith('_tests.exe') or
                        any(word in (p.get('CommandLine') or '') for word in
                            ('AutoFight.ps1', 'Build-Windows.ps1', 'RunnerJob.py', 'FightWatch.py', 'MenuDrive.py'))
                        if p.get('ProcessId') != os.getpid()],
    }


def recent_cycle_events(path):
    # Read only the recent tail, excluding huge tool results and full source-file copies.
    try:
        with path.open('rb') as stream:
            stream.seek(max(0, path.stat().st_size - 192000))
            lines = stream.read().decode('utf-8', errors='replace').splitlines()
    except OSError:
        return []
    result = []
    for line in lines:
        try:
            event = json.loads(line)
        except ValueError:
            continue
        part = event.get('part', {})
        if event.get('type') == 'text':
            result.append({'type': 'agent_text', 'text': part.get('text', '')[:1500]})
        elif event.get('type') == 'tool_use':
            state = part.get('state', {})
            result.append({'type': 'tool', 'tool': part.get('tool'), 'status': state.get('status'),
                           'input': str(state.get('input', {}))[:700],
                           'error': str(state.get('error', ''))[:400]})
        elif event.get('type') == 'error':
            result.append({'type': 'error', 'detail': str(event)[:700]})
    return result[-12:]


def snapshot():
    state = load(ROOT / '_Build' / 'agent-loop' / 'state.json', {})
    cycle_path = ROOT / '_Build' / 'agent-loop' / f"cycle-{state.get('cycles', 0):05d}.jsonl"
    result = {'checked_at': stamp(), 'target': 'correct sustained 20 FPS, then 30 FPS',
              'loop': {key: state.get(key) for key in
                       ('cycles', 'status', 'model', 'supervisor_pid', 'agent_pid', 'blockers', 'updated')},
              'cycle_log_age_seconds': age(cycle_path), 'cycle_recent_events': recent_cycle_events(cycle_path),
              'worker_heartbeat_age_seconds': age(ROOT / '_Build' / 'claude-runner' / 'heartbeat.txt'),
              'build_iteration': text(ROOT / 'build-iteration.txt', 40).strip(),
              'user_plan': text(ROOT / '_Build' / 'agent-reports' / 'OVERNIGHT-PLAN-20261006.md', 5000),
              'handoff': text(ROOT / 'HANDOFF.md', 7000), 'recent_jobs': [], 'gameplay': []}
    try:
        result['processes'] = roles(processes(), state.get('supervisor_pid'), state.get('agent_pid'))
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        result['process_error'] = str(error)
    done = ROOT / '_Build' / 'claude-runner' / 'done'
    exits = sorted(done.glob('*.exit'), key=lambda p: p.stat().st_mtime, reverse=True)
    result['completed_job_count'] = len(exits)
    for path in exits[:6]:
        result['recent_jobs'].append({'job': path.stem, 'exit': text(path, 40).strip(),
                                     'spec': load(path.with_suffix('.json'), {}),
                                     'log_tail': text(path.with_suffix('.log'), 2400, tail=True)})
    pending = ROOT / '_Build' / 'claude-runner' / 'queue'
    result['queued_jobs'] = [{'job': p.stem, 'spec': load(p, {})} for p in sorted(pending.glob('*.json'))[:5]]
    sessions = sorted((ROOT / '_Build' / 'autolab').glob('*/console.log'),
                      key=lambda p: p.stat().st_mtime, reverse=True)
    for path in sessions[:2]:
        folder = path.parent
        result['gameplay'].append({'session': folder.name, 'console_age_seconds': age(path),
                                  'summary': text(folder / 'summary.txt', 2600),
                                  'watch': load(folder / 'watch.json', {}),
                                  'console_tail': text(path, 2000, tail=True)})
    changes = [p for directory in ('src', 'tests') for p in (ROOT / directory).rglob('*')
               if p.is_file() and p.suffix in ('.cpp', '.h', '.inc', '.comp', '.py')]
    changes.sort(key=lambda p: p.stat().st_mtime, reverse=True)
    result['recent_source_changes'] = [{'path': str(p.relative_to(ROOT)), 'age_seconds': age(p)}
                                       for p in changes[:6]]
    return result


def parse_review(output):
    parts = []
    for line in output.splitlines():
        try:
            event = json.loads(line)
        except ValueError:
            continue
        if event.get('type') == 'error':
            raise ValueError(f"Provider error: {str(event)[:1000]}")
        if event.get('type') == 'text':
            parts.append(event.get('part', {}).get('text', ''))
    answer = ''.join(parts).strip()
    if answer.startswith('```'):
        answer = answer.split('\n', 1)[1].rsplit('```', 1)[0].strip()
    review = json.loads(answer)
    if not isinstance(review, dict) or review.get('status') not in ('on_track', 'attention', 'blocked', 'idle'):
        raise ValueError('Reviewer returned an invalid status')
    if any(not isinstance(review.get(key), str) or not review[key].strip()
           for key in ('summary', 'next_action')):
        raise ValueError('Reviewer summary/next_action missing')
    if not isinstance(review.get('evidence'), list) or any(not isinstance(x, str) for x in review['evidence']):
        raise ValueError('Reviewer evidence must be strings')
    return {'status': review['status'], 'summary': review['summary'][:1400],
            'evidence': [item[:700] for item in review['evidence'][:6]],
            'next_action': review['next_action'][:1800]}


def review_with_model(evidence, run_id, timeout):
    environment = os.environ.copy()
    environment.pop('OPENCODE_SERVER_PASSWORD', None)
    environment.pop('OPENCODE_SERVER_USERNAME', None)
    environment['OPENCODE_CLIENT'] = 'cli'
    environment['OPENCODE_DB'] = str(FOLDER / 'opencode-progress.db')
    environment['OPENCODE_CONFIG_CONTENT'] = json.dumps(
        {'small_model': MODEL, 'share': 'disabled', 'snapshot': False})
    # Attach text instead of putting the snapshot on Windows' length-limited command line.
    attachment = FOLDER / f'{run_id}.snapshot.txt'
    atomic_write(attachment, json.dumps(evidence, indent=2))
    prompt = ('Review the attached current UFC progress evidence snapshot. Return ONLY the JSON '
              'object defined by your agent instructions. No tools or edits.')
    command = [str(CLI), 'run', '--dir', str(ROOT), '--agent', 'ufc-progress', '--model', MODEL,
               '--format', 'json', '--file', str(attachment),
               '--title', 'UFC five-minute progress check', prompt]
    with (FOLDER / f'{run_id}.jsonl').open('w', encoding='utf-8') as output, \
            (FOLDER / f'{run_id}.stderr.log').open('w', encoding='utf-8') as errors:
        child = subprocess.Popen(command, cwd=ROOT, stdin=subprocess.DEVNULL,
                                 stdout=output, stderr=errors, env=environment)
        try:
            code = child.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            # This PID is exclusively our reviewer, never the lead/worker/game.
            subprocess.run(['taskkill', '/PID', str(child.pid), '/T', '/F'], capture_output=True)
            child.wait(timeout=15)
            raise TimeoutError('Free-model reviewer exceeded its bounded time limit')
    if code:
        # CLI provider failures are often JSON error events on stdout, not stderr.
        raw = text(FOLDER / f'{run_id}.jsonl', 100000, True)
        try:
            parse_review(raw)
        except ValueError as error:
            raise RuntimeError(f'Free-model reviewer exit {code}: {error}') from error
        raise RuntimeError(f'Free-model reviewer exit {code}: ' + text(FOLDER / f'{run_id}.stderr.log', 1000, True))
    return parse_review(text(FOLDER / f'{run_id}.jsonl', 100000, True))


def publish(report):
    atomic_write(FOLDER / 'latest.json', json.dumps(report, indent=2))
    lines = ['# UFC five-minute progress check', '', f"Checked: {report['checked_at']}",
             f"Model: `{MODEL}` (free; no paid fallback)", f"Status: **{report['status']}**", '',
             report['summary'], '']
    observed = report.get('observed', {})
    if observed:
        lines += ['## Directly collected facts (separate from model advice)',
                  f"- Lead cycle: {observed['loop'].get('cycles')}; state: {observed['loop'].get('status')}",
                  f"- Current build iteration: {observed['build_iteration']}"]
        for game in observed.get('gameplay', []):
            lines += [f"- {game['session']}: build {game.get('build', 'unknown')}; "
                      f"fight throughput {game.get('fight_frame_rate', 'not yet measured')} FPS; "
                      f"fight minimum {game.get('fight_min_fps', 'not yet measured')} FPS; "
                      f"observed fight {game.get('fight_observed_s', 'unknown')} s."]
        lines += ['']
    lines += ['## Reviewer evidence']
    lines += ['- ' + item for item in report.get('evidence', [])]
    lines += ['', '## Recommendation to the lead agent', report.get('next_action', ''), '',
              'Advisory only. Verify against the current plan and fresh evidence; this reviewer cannot change code or jobs.',
              'Different diagnostic captures are not a controlled A/B: FPS differences alone do not establish a regression or improvement.']
    atomic_write(FOLDER / 'latest.md', '\n'.join(lines) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--snapshot-only', action='store_true', help='check collection without invoking a model')
    parser.add_argument('--timeout', type=int, default=180)
    args = parser.parse_args()
    if not 15 <= args.timeout <= 210:
        parser.error('timeout must be between 15 and 210 seconds (less than five-minute cadence)')
    if not (ROOT / '_Build').is_dir():
        parser.error('configured project _Build directory is missing')
    FOLDER.mkdir(exist_ok=True)
    with (FOLDER / 'check.lock').open('a+b') as lock:
        import msvcrt
        lock.seek(0)
        if not lock.read(1):
            lock.write(b'0')
            lock.flush()
        lock.seek(0)
        try:
            msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
        except OSError:
            print('An existing progress check is still running; skipped.')
            return 0
        if (ROOT / '_Build' / 'agent-loop' / 'STOP').exists() or (ROOT / 'LOOP-GOAL-REACHED.md').exists():
            publish({'checked_at': stamp(), 'status': 'paused',
                     'summary': 'Loop intentionally stopped or goal marker present; no model invoked.',
                     'next_action': 'Wait for an explicit user restart.'})
            return 0
        run_id = datetime.now().strftime('check-%Y%m%d-%H%M%S')
        evidence = snapshot()
        atomic_write(FOLDER / f'{run_id}.snapshot.json', json.dumps(evidence, indent=2))
        if args.snapshot_only:
            print(json.dumps(evidence, indent=2))
            return 0
        try:
            review = review_with_model(evidence, run_id, args.timeout)
            games = []
            for game in evidence['gameplay']:
                fields = dict(line.split('=', 1) for line in game['summary'].splitlines() if '=' in line)
                games.append(dict(session=game['session'], **{key: fields[key] for key in
                    ('build', 'fight_frame_rate', 'fight_min_fps', 'fight_observed_s') if key in fields}))
            report = dict(review, checked_at=stamp(), model=MODEL, snapshot=f'{run_id}.snapshot.json',
                          observed={'loop': evidence['loop'], 'build_iteration': evidence['build_iteration'],
                                    'gameplay': games})
            atomic_write(FOLDER / f'{run_id}.review.json', json.dumps(report, indent=2))
            publish(report)
            print(json.dumps(report, indent=2))
            return 0
        except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
            report = {'checked_at': stamp(), 'status': 'unavailable', 'model': MODEL,
                      'summary': f'Free-model review unavailable: {error}',
                      'evidence': [f"Loop cycle {evidence['loop'].get('cycles')}: {evidence['loop'].get('status')}"],
                      'next_action': 'Continue the authorized lead work; retry this free reviewer on the next scheduled check.'}
            publish(report)
            print(json.dumps(report, indent=2))
            return 1


if __name__ == '__main__':
    raise SystemExit(main())
