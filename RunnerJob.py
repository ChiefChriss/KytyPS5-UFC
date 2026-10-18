"""Queue one job for the Claude job runner and wait for its result.

The Claude job runner (Start-Claude-Worker.cmd, _Build/claude-runner/Claude-JobRunner.ps1) is
the only process that builds, runs CTest or launches the game on this PC. Agents use this
script instead of calling Build-Windows.ps1, ctest or AutoFight.ps1 themselves.

Examples (run from the repo root with `py -3.13`):
  py -3.13 RunnerJob.py build --label dev095-fix
  py -3.13 RunnerJob.py build --label dev095-fix --kill-running   # user asked: closes our launcher first
  py -3.13 RunnerJob.py tests                      # full suite (builds kyty_tests first)
  py -3.13 RunnerJob.py tests --filter memory_tracker
  py -3.13 RunnerJob.py fight --label d094-bis-abcdefgh --monitor-seconds 300
  py -3.13 RunnerJob.py fight --label d095-tracy --tracy-seconds 30

Exit code = the runner's exit code (0 = success). 2 = runner not running, 3 = timed out.
"""
import argparse
from datetime import datetime
import json
from pathlib import Path
import re
import sys
import time

ROOT = Path(__file__).resolve().parent
RUNNER = ROOT / '_Build' / 'claude-runner'
SAFE = re.compile(r'^[A-Za-z0-9_.|\-]{0,120}$')


def heartbeat_age():
    try:
        return time.time() - (RUNNER / 'heartbeat.txt').stat().st_mtime
    except FileNotFoundError:
        return None


def read_text(path):
    data = path.read_bytes()
    for encoding in ('utf-8-sig', 'utf-16'):
        try:
            return data.decode(encoding)
        except UnicodeDecodeError:
            continue
    return data.decode('utf-8', errors='replace')


def make_spec(args):
    spec = {'action': args.action}
    if args.label:
        spec['label'] = args.label
    if args.filter:
        spec['filter'] = args.filter
    if args.kill_running:
        if args.action != 'build':
            sys.exit('--kill-running only applies to build')
        spec['kill_running'] = True
    if args.action == 'fight':
        spec['monitor_seconds'] = max(120, min(900, args.monitor_seconds))
        spec['tracy_seconds'] = max(0, min(60, args.tracy_seconds))
        if args.tracy_start_seconds is not None:
            if not 0 <= args.tracy_start_seconds <= 900:
                raise ValueError('tracy-start-seconds must be 0-900')
            spec['tracy_start_seconds'] = args.tracy_start_seconds
        if args.screenshot_every_sec is not None:
            if not 1 <= args.screenshot_every_sec <= 30:
                raise ValueError('screenshot-every-sec must be 1-30')
            spec['screenshot_every_sec'] = args.screenshot_every_sec
        if args.shader_dump:
            spec['shader_dump'] = True
    return spec


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('action', choices=['build', 'tests', 'fight', 'gitstatus'])
    parser.add_argument('--label', default='', help='fight label (selects KYTY_UFC_* switches, see AutoFight.ps1)')
    parser.add_argument('--filter', default='', help='ctest -R filter for the tests action')
    parser.add_argument('--monitor-seconds', type=int, default=300, help='fight length, 120-900')
    parser.add_argument('--tracy-seconds', type=int, default=0, help='Tracy capture length, 0-60')
    parser.add_argument('--tracy-start-seconds', type=int, default=None, help='early capture trigger, 0-900')
    parser.add_argument('--screenshot-every-sec', type=int, default=None, help='screenshot interval, 1-30')
    parser.add_argument('--shader-dump', action='store_true', help='save guest/native shaders for diagnosis')
    parser.add_argument('--timeout', type=int, default=0, help='seconds to wait (default per action)')
    parser.add_argument('--no-wait', action='store_true', help='queue and return the job name')
    parser.add_argument('--kill-running', action='store_true',
                        help='build only: close this checkout\'s launcher/emulator first. Only when the user asked '
                             'for the build; never while they may be playing')
    args = parser.parse_args()

    for value in (args.label, args.filter):
        if not SAFE.match(value):
            sys.exit(f'unsafe label/filter {value!r}: use only letters, digits, _ . | -')
    if args.action == 'fight' and not args.label:
        sys.exit('a fight needs --label')

    try:
        spec = make_spec(args)
    except ValueError as error:
        parser.error(str(error))
    age = heartbeat_age()
    if age is None or age > 60:
        print('Claude job runner is not running (no heartbeat in the last 60 s). Ask the user to start '
              'Start-Claude-Worker.cmd (or press Enter in its window if it is paused). Do not build, '
              'test or launch the game yourself.', file=sys.stderr)
        return 2

    queue = RUNNER / 'queue'
    done = RUNNER / 'done'
    queue.mkdir(parents=True, exist_ok=True)
    name = f'oc{datetime.now():%Y%m%d-%H%M%S}-{args.action}'
    temporary = queue / f'{name}.tmp'
    temporary.write_text(json.dumps(spec), encoding='ascii')
    temporary.replace(queue / f'{name}.json')   # the runner only picks up *.json
    ahead = sorted(p.name for p in queue.glob('*.json') if p.stem < name)
    print(f'queued {name}: {json.dumps(spec)}' + (f' (behind {len(ahead)} job(s))' if ahead else ''), flush=True)
    if args.no_wait:
        print(name)
        return 0

    timeout = args.timeout or {'build': 2400, 'tests': 2400, 'gitstatus': 300}.get(
        args.action, spec.get('monitor_seconds', 300) + 900)
    exit_file = done / f'{name}.exit'
    started = time.time()
    warned = False
    while not exit_file.exists():
        if time.time() - started > timeout:
            print(f'timed out after {timeout} s waiting for {name}; it may still run. '
                  f'Check {exit_file} later.', file=sys.stderr)
            return 3
        age = heartbeat_age()
        if age is not None and age > 120 and not warned and (queue / f'{name}.json').exists():
            print('warning: the runner heartbeat is stale; its window may be paused (press Enter in it) '
                  'or closed.', file=sys.stderr, flush=True)
            warned = True
        time.sleep(5)
    time.sleep(1)
    try:
        code = int(read_text(exit_file).strip() or '1')
    except ValueError:
        code = 1
    log = done / f'{name}.log'
    text = read_text(log) if log.exists() else ''
    lines = text.splitlines()
    print(f'{name} finished, exit {code}, log {log}')
    if args.action == 'fight':
        keep = [line for line in lines if not line.startswith('gpu_')]
        print('\n'.join(keep[-60:]))
    else:
        print('\n'.join(lines[-40:]))
    error_log = done / f'{name}.log.err'
    if code != 0 and error_log.exists():
        print('--- stderr ---')
        print('\n'.join(read_text(error_log).splitlines()[-20:]))
    return code


if __name__ == '__main__':
    raise SystemExit(main())
