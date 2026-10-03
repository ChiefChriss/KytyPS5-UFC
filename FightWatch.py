"""Local UFC HUD detector and incremental console monitor; no model/API calls.

Paired stamina bars AND fighter-name regions identify a likely fight. Two consecutive
screenshots are required. This is a heuristic, not proof of correct graphics/gameplay.
Fatal console evidence is latched in watch.json for AutoFight to end the run early.
"""
import argparse
import json
from pathlib import Path
import re
import time

from PIL import Image

FATAL = re.compile(r'ErrorDeviceLost|VK_ERROR_DEVICE_LOST|--- Fatal Error ---|'
                   r'\[FATAL\]|SPIR-V validation failed|Validation Error|Assertion failed', re.I)


def detect_fight(path):
    with Image.open(path) as image:
        image = image.convert('RGB').resize((400, 230))
        def fraction(box, predicate):
            pixels = list(image.crop(box).getdata())
            return sum(predicate(*p) for p in pixels) / max(1, len(pixels))
        green = lambda r, g, b: g > 145 and r > 90 and b < 125 and g > r * 0.85
        white = lambda r, g, b: min(r, g, b) > 190 and max(r, g, b) - min(r, g, b) < 45
        bars = [fraction((22, 21, 128, 29), green), fraction((272, 21, 378, 29), green)]
        names = [fraction((20, 11, 128, 21), white), fraction((272, 11, 378, 21), white)]
        return {'fight_candidate': min(bars) > 0.08 and min(names) > 0.015,
                'stamina_fractions': bars, 'name_fractions': names}


class LogTail:
    def __init__(self, path):
        self.path, self.offset, self.pending = path, 0, b''

    def read(self):
        if not self.path.exists():
            return []
        if self.path.stat().st_size < self.offset:
            self.offset, self.pending = 0, b''
        with self.path.open('rb') as stream:
            stream.seek(self.offset)
            data = stream.read(64 * 1024)
            self.offset = stream.tell()
        lines = (self.pending + data).split(b'\n')
        self.pending = lines.pop()
        return [line.decode('utf-8', errors='replace').strip() for line in lines]


def publish(folder, state):
    temporary = folder / 'watch.tmp.json'
    temporary.write_text(json.dumps(state, indent=2), encoding='utf-8')
    temporary.replace(folder / 'watch.json')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--session', type=Path, required=True)
    parser.add_argument('--start-epoch', type=float, required=True)
    args = parser.parse_args()
    tails = [LogTail(args.session / name) for name in ('console.log', 'stderr.log')]
    seen, consecutive = set(), 0
    state = {'fight_detected': False, 'first_fight_s': None, 'action': 'continue', 'reason': ''}
    while True:
        for tail in tails:
            for line in tail.read():
                if FATAL.search(line):
                    state.update(action='stop', reason=line, evidence_file=tail.path.name)
                    (args.session / 'correction-request.md').write_text(
                        '# Correction required\n\n' + f'Source: {tail.path.name}\n\n```text\n{line}\n```\n'
                        'Read surrounding console output, fix the cause, rebuild, run regressions, and rerun.\n',
                        encoding='utf-8')
                    publish(args.session, state)
                    print(f'correction required: {line}', flush=True)
                    return
        for shot in sorted(args.session.glob('shot_*.png')):
            if shot.name in seen:
                continue
            try:
                detection = detect_fight(shot)
            except (OSError, ValueError):
                continue  # Screenshot may still be being written; retry next poll.
            seen.add(shot.name)
            consecutive = consecutive + 1 if detection['fight_candidate'] else 0
            state.update(last_screenshot=shot.name, hud=detection)
            if consecutive >= 2 and not state['fight_detected']:
                state.update(fight_detected=True, first_fight_s=round(time.time() - args.start_epoch, 2))
                print(f'fight HUD detected at {state["first_fight_s"]}s ({shot.name})', flush=True)
        publish(args.session, state)
        time.sleep(0.5 if state['fight_detected'] else 1.0)


if __name__ == '__main__':
    main()
