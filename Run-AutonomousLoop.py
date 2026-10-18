"""Serial OpenCode supervisor with persisted quota cooldowns. No credentials stored.

Default: unlimited focused local cycles. Create _Build/agent-loop/STOP to stop between
cycles, or LOOP-GOAL-REACHED.md when verified objectives are achieved. Sol becomes
eligible five hours after a quota error, even while a fallback continues working.
"""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parent
MODELS = ('openai/gpt-6.1-sol', 'openrouter/deepseek/deepseek-v4.1-flash')
QUOTA = re.compile(r'quota|credits?|usage.limit|limit.reached|rate.limit|429|insufficient.funds', re.I)
PROMPT = ('FIRST read _Build/agent-reports/OVERNIGHT-PLAN-20261006.md. The user is asleep and '
          'explicitly authorized unattended corrective work and upgrades: the blackout occurred '
          'immediately on loading into gameplay. The immediate measured target is media/intro '
          'playback and sustained visually correct 25 FPS; 30 FPS is the longer-term objective. '
          'This plan overrides the old three-baseline stop and Task B/C deferral. Read only the '
          'latest HANDOFF.md/LOOP-LOG.md entries and ../transfer-settings.json for current context. '
          'If blackout reproduction is bounded or inconclusive, continue independently with '
          'intro/media profiling and evidenced per-draw/readback optimizations; do not ask for '
          'routine input or repeat status-only cycles. Each invocation must advance one concrete '
          'milestone with a targeted capture, failing regression, bounded engine correction or '
          'necessary verification. All builds, CTest and game launches use the Claude worker via '
          'py -3.13 RunnerJob.py; never run the build/test/game scripts directly or stop the worker. '
          'Respect manual play and serialize jobs. Preserve existing uncommitted work, red-zone '
          'protection, validation, wave32 mask branches, BDA batch and runtime viewport fixes. '
          'No resolution/quality cuts, skipped guest work, shader clamps, stale readbacks, commits '
          'or pushes. Record measured results and next action once per meaningful milestone, '
          'without declaring media playback or 25 FPS achieved before repeated actual evidence. '
          'Do not start another supervisor or launch other coding agents.')


def command(model, continuation=''):
    executable = shutil.which('opencode')
    if executable is None:
        raise RuntimeError('opencode is not on PATH')
    args = [executable, 'run', '--dir', str(ROOT), '--agent', 'ufc-loop',
            '--model', model, '--format', 'json']
    if model.startswith('openai/'):
        args += ['--variant', 'high']
    args += [PROMPT + (' ' + continuation if continuation else '')]
    if os.name == 'nt' and executable.lower().endswith(('.cmd', '.bat')):
        return [os.environ.get('COMSPEC', 'cmd.exe'), '/d', '/s', '/c', subprocess.list2cmdline(args)]
    return args


def utc_stamp():
    return datetime.now(timezone.utc).isoformat()


def choose_model(cooldown, now):
    return next((model for model in MODELS if cooldown.get(model, 0) <= now), None)


def child_environment(folder):
    environment = os.environ.copy()
    # The desktop server's Basic Auth env is not applicable to an in-process CLI server.
    # Use a separate database: this machine's shared desktop DB lacks replacement_seq.
    environment.pop('OPENCODE_SERVER_PASSWORD', None)
    environment.pop('OPENCODE_SERVER_USERNAME', None)
    environment['OPENCODE_CLIENT'] = 'cli'
    environment['OPENCODE_DB'] = str(folder / 'opencode-loop.db')
    return environment


def save(path, state):
    temporary = path.with_suffix('.tmp')
    temporary.write_text(json.dumps(state, indent=2), encoding='utf-8')
    temporary.replace(path)


def error_text(output):
    """Only classify error events, not ordinary model/tool text mentioning quotas."""
    errors = []
    for line in output.splitlines():
        try:
            event = json.loads(line)
        except (ValueError, TypeError):
            continue
        if event.get('type') == 'error':
            errors.append(json.dumps(event, ensure_ascii=False))
    return '\n'.join(errors)


def process_blockers(processes, ignored_pids=()):
    """At a cycle boundary, every existing game/test/build belongs to someone else."""
    blockers = []
    for process in processes:
        name = (process.get('Name') or '').lower()
        text = process.get('CommandLine') or ''
        busy = name in ('launcher.exe', 'kyty_emulator.exe', 'ctest.exe', 'cmake.exe',
                        'ninja.exe', 'clang-cl.exe', 'lld-link.exe') or name.endswith('_tests.exe')
        if process.get('ProcessId') not in ignored_pids and name in (
                    'powershell.exe', 'pwsh.exe', 'cmd.exe', 'python.exe', 'pythonw.exe',
                    'py.exe', 'node.exe'):
            # The Claude job runner itself is expected (agents queue work to it); its jobs are not.
            busy |= bool(re.search(r'(Build-Windows|AutoFight|MenuDrive|FightWatch)\.(ps1|py)\b', text, re.I))
        if process.get('ProcessId') not in ignored_pids and name in (
                    'opencode.exe', 'opencode-cli.exe', 'node.exe', 'cmd.exe'):
            busy |= 'opencode' in text.lower() and bool(re.search(r'\brun\s', text))
        if busy:
            blockers.append(f"{process.get('Name')} PID {process.get('ProcessId')}")
    return blockers


def input_blocker():
    request = ROOT / '_Build' / 'agent-loop' / 'WAIT-FOR-INPUT.json'
    try:
        data = json.loads(request.read_text(encoding='utf-8-sig'))
        reason = data.get('reason') if isinstance(data, dict) else None
        if not isinstance(reason, str) or not reason.strip():
            raise ValueError('the input request needs a nonempty reason')
        return f'Human input required: {reason.strip()}'
    except FileNotFoundError:
        return None
    except (OSError, ValueError) as error:
        return f'Human input required: cannot read {request.name}: {error}'


def process_snapshot():
    script = ("$ErrorActionPreference = 'Stop'; "
              "[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new(); "
              "ConvertTo-Json -Compress -InputObject @(Get-CimInstance Win32_Process | "
              "Select-Object Name,ProcessId,ParentProcessId,CommandLine)")
    result = subprocess.run(['powershell.exe', '-NoProfile', '-NonInteractive',
                             '-Command', script], capture_output=True, text=True,
                            encoding='utf-8', timeout=30, check=True)
    processes = json.loads(result.stdout)
    if not isinstance(processes, list) or any(not isinstance(p, dict) for p in processes):
        raise ValueError('process snapshot is not a JSON object array')
    return processes


def recover_runner(state):
    """Start only an absent project worker; never terminate a live/paused worker or game."""
    if os.name != 'nt' or state.get('runner_retry_after', 0) > time.time():
        return
    runner = ROOT / '_Build' / 'claude-runner'
    try:
        if time.time() - (runner / 'heartbeat.txt').stat().st_mtime <= 60:
            return
    except FileNotFoundError:
        pass
    processes = process_snapshot()
    pattern = r'Claude-JobRunner\.ps1|Start-Transfer\.ps1.*-Mode\s+[\"\x27]?Claude\b'
    if any(re.search(pattern, p.get('CommandLine') or '', re.I) for p in processes):
        return  # A live worker might be executing a long job or paused in its console.
    if process_blockers(processes):
        return
    shortcut = ROOT.parent / 'Start-Claude-Worker.cmd'
    if not shortcut.is_file() or not (ROOT / '_Build' / 'windows' / 'CMakeCache.txt').is_file():
        return
    state['runner_retry_after'] = time.time() + 120
    child = subprocess.Popen([os.environ.get('COMSPEC', 'cmd.exe'), '/c', str(shortcut)],
                             cwd=ROOT.parent, creationflags=subprocess.CREATE_NEW_CONSOLE)
    state['runner_console_pid'] = child.pid
    print(f'{utc_stamp()} started absent Claude worker console PID {child.pid}', flush=True)


def work_snapshot():
    """Track engine/tests and completed useful jobs, excluding repeated status documentation."""
    entries = []
    paths = []
    for directory in ('src', 'tests'):
        paths.extend(path for path in (ROOT / directory).rglob('*')
                     if path.is_file() and path.suffix in ('.cpp', '.h', '.inc', '.comp', '.py'))
    paths.extend((ROOT / '_Build' / 'autolab').glob('*/summary.txt'))
    done = ROOT / '_Build' / 'claude-runner' / 'done'
    for exit_file in done.glob('*.exit'):
        try:
            job = json.loads(exit_file.with_suffix('.json').read_text(encoding='utf-8-sig'))
            if isinstance(job, dict) and job.get('action') in ('build', 'tests', 'fight'):
                paths.append(exit_file)
        except (OSError, ValueError):
            continue
    for path in paths:
        try:
            stat = path.stat()
            entries.append((str(path.relative_to(ROOT)), stat.st_mtime_ns, stat.st_size))
        except FileNotFoundError:
            continue
    return tuple(sorted(entries))


def note_progress(state, model, before, after, now):
    if before != after:
        state['no_progress_cycles'] = 0
        return False
    state['no_progress_cycles'] = state.get('no_progress_cycles', 0) + 1
    if state['no_progress_cycles'] >= 2:
        state['cooldown'][model] = now + 300
        return True
    return False


def set_awake(enabled):
    if os.name != 'nt':
        return
    import ctypes
    call = ctypes.windll.kernel32.SetThreadExecutionState
    call.argtypes = [ctypes.c_uint32]
    call.restype = ctypes.c_uint32
    # A process-scoped request; no persistent power-plan or registry changes.
    if not call(0x80000000 | (0x00000001 if enabled else 0)):
        raise ctypes.WinError()


def preflight_blockers():
    """Fail closed if process ownership cannot be checked; never stop foreign processes."""
    waiting = input_blocker()
    if waiting:
        return [waiting]
    blockers = []
    # Agents build, test and fight only through the Claude job runner (RunnerJob.py), so the
    # runner must be alive and idle at a cycle boundary.
    runner = ROOT / '_Build' / 'claude-runner'
    heartbeat = runner / 'heartbeat.txt'
    try:
        age = time.time() - heartbeat.stat().st_mtime
        if age > 120:
            blockers.append(f'Claude job runner is not running or paused (heartbeat {age:.0f}s old); '
                            'start Start-Claude-Worker.cmd or press Enter in its window')
    except FileNotFoundError:
        blockers.append('Claude job runner has never run here; start Start-Claude-Worker.cmd')
    except OSError as error:
        blockers.append(f'Cannot check Claude runner heartbeat: {error}')
    pending = sorted(p.name for p in (runner / 'queue').glob('*.json'))
    if pending:
        blockers.append(f'runner queue still has {len(pending)} job(s): {", ".join(pending[:3])}')
    if os.name != 'nt':
        return blockers + ['Process pre-flight currently requires Windows']
    try:
        processes = process_snapshot()
        # Our calling shell/agent may mention future build/game commands in its arguments.
        # Ignore those wrappers, but still block every game/build/test executable.
        parents = {p.get('ProcessId'): p.get('ParentProcessId') for p in processes}
        ancestors = set()
        pid = os.getpid()
        while pid and pid not in ancestors:
            ancestors.add(pid)
            pid = parents.get(pid)
        blockers.extend(process_blockers(processes, ancestors))
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        blockers.append(f'Cannot verify active processes: {error}')
    return blockers


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--max-cycles', type=int, default=0, help='0 = unlimited')
    parser.add_argument('--retry-hours', type=float, default=5)
    parser.add_argument('--cycle-timeout', type=int, default=3600)
    parser.add_argument('--dry-run', action='store_true')
    parser.add_argument('--preflight-only', action='store_true', help='check blockers without launching an agent')
    parser.add_argument('--background', action='store_true', help='start detached and return its PID')
    parser.add_argument('--keep-awake', action='store_true', help='prevent system sleep while supervising')
    parser.add_argument('--recover-runner', action='store_true', help='start the project worker if absent')
    args = parser.parse_args()
    if args.max_cycles < 0 or args.retry_hours <= 0 or args.cycle_timeout <= 0:
        parser.error('invalid cycle count, retry period, or timeout')
    if args.preflight_only:
        blockers = preflight_blockers()
        print(json.dumps({'clear': not blockers, 'blockers': blockers}, indent=2))
        raise SystemExit(1 if blockers else 0)
    folder = ROOT / '_Build' / 'agent-loop'
    folder.mkdir(parents=True, exist_ok=True)
    if args.background:
        flags = subprocess.DETACHED_PROCESS | subprocess.CREATE_NEW_PROCESS_GROUP if os.name == 'nt' else 0
        with (folder / 'supervisor.log').open('a', encoding='utf-8') as output, (folder / 'supervisor.stderr.log').open('a', encoding='utf-8') as errors:
            arguments = [os.sys.executable, str(Path(__file__).resolve()),
                         '--max-cycles', str(args.max_cycles), '--retry-hours', str(args.retry_hours),
                         '--cycle-timeout', str(args.cycle_timeout)]
            if args.keep_awake:
                arguments.append('--keep-awake')
            if args.recover_runner:
                arguments.append('--recover-runner')
            child = subprocess.Popen(arguments, cwd=ROOT,
                                     stdin=subprocess.DEVNULL, stdout=output, stderr=errors,
                                     creationflags=flags, close_fds=True)
        print(f'Detached supervisor PID: {child.pid}', flush=True)
        return
    state_path = folder / 'state.json'
    state = json.loads(state_path.read_text()) if state_path.exists() else {'cycles': 0, 'cooldown': {}}
    if args.dry_run:
        print(json.dumps({'models': MODELS, 'retry_hours': args.retry_hours,
                          'commands': [command(model) for model in MODELS]}, indent=2))
        return
    # An OS-owned byte lock is released even if the supervisor crashes; stale files are harmless.
    with (folder / 'supervisor.lock').open('a+b') as lock_file:
        if os.name == 'nt':
            import msvcrt
            lock_file.seek(0)
            if not lock_file.read(1):
                lock_file.write(b'0')
                lock_file.flush()
            lock_file.seek(0)
            try:
                msvcrt.locking(lock_file.fileno(), msvcrt.LK_NBLCK, 1)
            except OSError:
                raise SystemExit('A supervisor already owns this workspace')
        if args.keep_awake:
            set_awake(True)
        try:
            run_cycles(args, folder, state_path, state)
        finally:
            if args.keep_awake:
                set_awake(False)


def run_cycles(args, folder, state_path, state):
    state.pop('agent_pid', None)
    state.update(supervisor_pid=os.getpid(), status='starting', updated=utc_stamp(),
                 model_priority=list(MODELS), keep_awake=args.keep_awake,
                 recover_runner=args.recover_runner)
    save(state_path, state)
    attempted = 0
    while args.max_cycles == 0 or attempted < args.max_cycles:
        if (folder / 'STOP').exists() or (ROOT / 'LOOP-GOAL-REACHED.md').exists():
            state.update(status='stopped', updated=utc_stamp())
            save(state_path, state)
            return
        if args.recover_runner:
            try:
                recover_runner(state)
            except (OSError, ValueError, subprocess.SubprocessError) as error:
                print(f'{utc_stamp()} cannot recover worker: {error}', flush=True)
        blockers = preflight_blockers()
        if blockers:
            status = ('waiting for user input' if any(
                blocker.startswith('Human input required:') for blocker in blockers)
                else 'waiting for pre-flight')
            if state.get('blockers') != blockers:
                print(f'{utc_stamp()} {status}: {"; ".join(blockers)}', flush=True)
            state.update(status=status, blockers=blockers, updated=utc_stamp())
            save(state_path, state)
            time.sleep(60)
            continue
        state.pop('blockers', None)
        now = time.time()
        model = choose_model(state['cooldown'], now)
        if model is None:
            state.update(status='waiting for quota cooldown', updated=utc_stamp())
            save(state_path, state)
            # Saved cooldowns may include retired fallback models; only the current policy
            # determines when work can resume.
            next_ready = min(state['cooldown'][candidate] for candidate in MODELS)
            time.sleep(min(60, max(1, next_ready - now)))
            continue
        attempted += 1
        before_work = work_snapshot()
        state['cycles'] += 1
        cycle = state['cycles']
        output_path = folder / f'cycle-{cycle:05d}.jsonl'
        errors_path = folder / f'cycle-{cycle:05d}.stderr.log'
        state.update(status='running', model=model, updated=utc_stamp(), output=str(output_path))
        save(state_path, state)
        print(f'{utc_stamp()} cycle {cycle}: {model}', flush=True)
        continuation = ('Previous cycles completed no useful runner job or engine/test change. '
                        'Choose a permitted independent milestone from the overnight plan and '
                        'execute its next concrete diagnostic or correction instead of repeating '
                        'the old blackout blocker.' if state.get('no_progress_cycles') else '')
        with output_path.open('w', encoding='utf-8') as stdout, errors_path.open('w', encoding='utf-8') as stderr:
            child = subprocess.Popen(command(model, continuation), cwd=ROOT, stdin=subprocess.DEVNULL,
                                     stdout=stdout, stderr=stderr, env=child_environment(folder))
            state.update(agent_pid=child.pid)
            save(state_path, state)
            try:
                code = child.wait(timeout=args.cycle_timeout)
            except subprocess.TimeoutExpired:
                if os.name == 'nt':
                    subprocess.run(['taskkill', '/PID', str(child.pid), '/T', '/F'], capture_output=True)
                else:
                    child.kill()
                child.wait()
                code = -1
        # Logs remain on disk; only error events influence model selection.
        error = error_text(output_path.read_text(encoding='utf-8', errors='replace'))
        if code != 0:
            error += '\n' + errors_path.read_text(encoding='utf-8', errors='replace')
        state.update(last_exit_code=code, last_error=error[-4000:], updated=utc_stamp())
        if error or code != 0:
            cooldown = args.retry_hours * 3600 if QUOTA.search(error) else 300
            state['cooldown'][model] = time.time() + cooldown
            state['status'] = 'model cooling down'
            print(f'cycle {cycle}: provider/run error; cooldown {cooldown:g}s', flush=True)
        else:
            idle = note_progress(state, model, before_work, work_snapshot(), time.time())
            state['status'] = 'no-progress cooldown' if idle else 'cycle finished'
            if idle:
                print(f'cycle {cycle}: no useful work; trying an eligible fallback after cooldown', flush=True)
        state.pop('agent_pid', None)
        save(state_path, state)
        time.sleep(5)


if __name__ == '__main__':
    main()
