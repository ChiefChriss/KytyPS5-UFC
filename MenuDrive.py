"""Drive emulator menus with a virtual DS4 (ViGEmBus + vgamepad).
Presses a button on a schedule so fights can be started unattended.
Usage: py -3.13 MenuDrive.py [--button cross|options] [--interval 2.5] [--count 7] [--hold 0.25]
       py -3.13 MenuDrive.py --schedule "9:1x0,1.5:3x1.5,3:8x1.5" [--start-epoch <unix time>]

A schedule is comma-separated groups of WAIT:COUNTxINTERVAL. WAIT is the delay before the
group's first press, measured from the previous press (or from the start time for the first
group); the group then presses COUNT times, INTERVAL seconds apart. Times are press-to-press.
"""
import argparse, math, sys, time
try:
    import vgamepad as vg
except ImportError:
    print('vgamepad missing: py -3.13 -m pip install vgamepad', file=sys.stderr)
    sys.exit(2)

BUTTONS = {
    'cross': vg.DS4_BUTTONS.DS4_BUTTON_CROSS,
    'circle': vg.DS4_BUTTONS.DS4_BUTTON_CIRCLE,
    'options': vg.DS4_BUTTONS.DS4_BUTTON_OPTIONS,
    'triangle': vg.DS4_BUTTONS.DS4_BUTTON_TRIANGLE,
    'square': vg.DS4_BUTTONS.DS4_BUTTON_SQUARE,
}
DPAD = {
    'up': vg.DS4_DPAD_DIRECTIONS.DS4_BUTTON_DPAD_NORTH,
    'down': vg.DS4_DPAD_DIRECTIONS.DS4_BUTTON_DPAD_SOUTH,
    'left': vg.DS4_DPAD_DIRECTIONS.DS4_BUTTON_DPAD_WEST,
    'right': vg.DS4_DPAD_DIRECTIONS.DS4_BUTTON_DPAD_EAST,
}


def press(pad, name, hold):
    if name in DPAD:
        pad.directional_pad(direction=DPAD[name]); pad.update()
        time.sleep(hold)
        pad.directional_pad(direction=vg.DS4_DPAD_DIRECTIONS.DS4_BUTTON_DPAD_NONE); pad.update()
    else:
        pad.press_button(button=BUTTONS[name]); pad.update()
        time.sleep(hold)
        pad.release_button(button=BUTTONS[name]); pad.update()

def parse_actions(text, default_hold=0.25):
    """Return (target seconds, hold seconds); groups optionally end with @HOLD."""
    actions = []
    last = 0.0
    for group in filter(None, (g.strip() for g in text.split(','))):
        try:
            wait, rest = group.split(':')
            rest, separator, hold_text = rest.partition('@')
            hold = float(hold_text) if separator else default_hold
            count, interval = rest.lower().split('x')
            wait, count, interval = float(wait), int(count), float(interval)
        except ValueError:
            raise SystemExit(f'bad schedule group "{group}", expected WAIT:COUNTxINTERVAL')
        if count < 1 or not all(math.isfinite(v) for v in (wait, interval, hold)) or wait < 0 or interval < 0 or hold <= 0:
            raise SystemExit(f'bad schedule group "{group}"')
        if count > 1 and interval <= hold:
            raise SystemExit(f'interval must exceed hold in "{group}"')
        t = last + wait
        for _ in range(count):
            if actions and t < actions[-1][0] + actions[-1][1]:
                raise SystemExit(f'overlapping presses in "{group}"')
            actions.append((t, hold))
            last = t
            t += interval
    return actions

def parse_schedule(text):
    return [target for target, _ in parse_actions(text)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--button', default='cross')
    ap.add_argument('--interval', type=float, default=2.5)
    ap.add_argument('--count', type=int, default=7)
    ap.add_argument('--schedule', default='', help='overrides --count/--interval')
    ap.add_argument('--start-epoch', type=float, default=0.0,
                    help='unix time the schedule is relative to (default: now)')
    ap.add_argument('--hold', type=float, default=0.25)
    ap.add_argument('--log-csv', default='')
    a = ap.parse_args()
    if a.button not in BUTTONS and a.button not in DPAD:
        print('unknown button', a.button); sys.exit(1)
    if a.schedule:
        actions = parse_actions(a.schedule, a.hold)
    else:
        if a.count < 1 or not math.isfinite(a.hold) or a.hold <= 0 or not math.isfinite(a.interval) or a.interval <= a.hold:
            ap.error('count must be positive and interval must exceed a positive hold')
        actions = [(1.0 + i * a.interval, a.hold) for i in range(a.count)]
    if not actions:
        print('empty schedule'); sys.exit(1)
    start = a.start_epoch or time.time()
    try:
        pad = vg.VDS4Gamepad()
    except Exception as e:
        print(f'cannot create virtual DS4 (is ViGEmBus installed?): {e}', file=sys.stderr)
        sys.exit(3)
    print(f'virtual DS4 ready: {len(actions)}x {a.button} at ' +
          ', '.join(f'{t:g}s/{hold:g}s hold' for t, hold in actions), flush=True)
    fh = open(a.log_csv, 'w') if a.log_csv else None
    if fh:
        fh.write('press,target_s,elapsed_s,hold_s\n')
    for i, (target, hold) in enumerate(actions):
        delay = start + target - time.time()
        if delay > 0:
            time.sleep(delay)
        el = time.time() - start
        press(pad, a.button, hold)
        print(f'press {i+1}/{len(actions)} target={target:.2f}s t={el:.2f}s hold={hold:.2f}s', flush=True)
        if fh:
            fh.write(f'{i+1},{target:.2f},{el:.2f},{hold:.2f}\n'); fh.flush()
    print(f'done total={time.time() - start:.1f}s', flush=True)
    if fh:
        fh.close()

if __name__ == '__main__':
    main()
