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
MODELS = ('openai/gpt-6.1-sol', 'meta/muse-spark-1.3',
          'opencode/muse-spark-1.3-contributor-free')
QUOTA = re.compile(r'quota|credits?|usage.limit|limit.reached|rate.limit|429|insufficient.funds', re.I)
PROMPT = ('ENGINE OPTIMIZATION ONLY: work on emulator C++/shader engine code, not automation tooling. '
          'Read HANDOFF.md, LOOP-LOG.md, UPSTREAM-COMPARISON.md. First obtain a clean actual UFC 5 '
          'fight profile on the merged dev build if none exists; then choose the highest-confidence '
          'CPU/GPU engine hotspot and make one narrowly scoped optimization preserving correctness/image '
          'quality. Do not alter resolution, skip guest work, clamp a shader, or weaken validation to fake FPS. '
          'Build, run relevant tests and full CTest, then compare a clean same-settings gameplay run. '
          'Record measured evidence and next engine hypothesis in LOOP-LOG.md. Preserve existing work. '
          'Do not ask for approval or start another loop.')


def command(model):
    executable = shutil.which('opencode')
    if executable is None:
        raise RuntimeError('opencode is not on PATH')
    args = [executable, 'run', '--dir', str(ROOT), '--agent', 'ufc-loop',
            '--model', model, '--format', 'json']
    if model.startswith('openai/'):
        args += ['--variant', 'high']
    args += [PROMPT]
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


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--max-cycles', type=int, default=0, help='0 = unlimited')
    parser.add_argument('--retry-hours', type=float, default=5)
    parser.add_argument('--cycle-timeout', type=int, default=3600)
    parser.add_argument('--dry-run', action='store_true')
    parser.add_argument('--background', action='store_true', help='start detached and return its PID')
    args = parser.parse_args()
    if args.max_cycles < 0 or args.retry_hours <= 0 or args.cycle_timeout <= 0:
        parser.error('invalid cycle count, retry period, or timeout')
    folder = ROOT / '_Build' / 'agent-loop'
    folder.mkdir(parents=True, exist_ok=True)
    if args.background:
        flags = subprocess.DETACHED_PROCESS | subprocess.CREATE_NEW_PROCESS_GROUP if os.name == 'nt' else 0
        with (folder / 'supervisor.log').open('a', encoding='utf-8') as output, (folder / 'supervisor.stderr.log').open('a', encoding='utf-8') as errors:
            child = subprocess.Popen([os.sys.executable, str(Path(__file__).resolve()),
                                      '--max-cycles', str(args.max_cycles), '--retry-hours', str(args.retry_hours),
                                      '--cycle-timeout', str(args.cycle_timeout)], cwd=ROOT,
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
    lock_file = (folder / 'supervisor.lock').open('a+b')
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
    state.update(supervisor_pid=os.getpid(), status='starting', updated=utc_stamp())
    save(state_path, state)
    attempted = 0
    while args.max_cycles == 0 or attempted < args.max_cycles:
        if (folder / 'STOP').exists() or (ROOT / 'LOOP-GOAL-REACHED.md').exists():
            state.update(status='stopped', updated=utc_stamp())
            save(state_path, state)
            return
        now = time.time()
        model = choose_model(state['cooldown'], now)
        if model is None:
            state.update(status='waiting for quota cooldown', updated=utc_stamp())
            save(state_path, state)
            time.sleep(min(60, max(1, min(state['cooldown'].values()) - now)))
            continue
        attempted += 1
        state['cycles'] += 1
        cycle = state['cycles']
        output_path = folder / f'cycle-{cycle:05d}.jsonl'
        errors_path = folder / f'cycle-{cycle:05d}.stderr.log'
        state.update(status='running', model=model, updated=utc_stamp(), output=str(output_path))
        save(state_path, state)
        print(f'{utc_stamp()} cycle {cycle}: {model}', flush=True)
        with output_path.open('w', encoding='utf-8') as stdout, errors_path.open('w', encoding='utf-8') as stderr:
            child = subprocess.Popen(command(model), cwd=ROOT, stdin=subprocess.DEVNULL,
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
            state['status'] = 'cycle finished'
        state.pop('agent_pid', None)
        save(state_path, state)
        time.sleep(5)


if __name__ == '__main__':
    main()
