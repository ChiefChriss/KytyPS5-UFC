"""Compare inclusive Tracy CPU zone costs, merging duplicate per-thread source rows."""
import argparse
import csv
from pathlib import Path


def aggregate(rows):
    zones = {}
    for row in rows:
        key = (row['name'], row['src_file'].replace('\\', '/'), row['src_line'])
        total, count = int(row['total_ns']), int(row['counts'])
        if total < 0 or count < 0 or (count == 0 and total != 0):
            raise ValueError('Invalid Tracy total/count')
        previous = zones.setdefault(key, [0, 0])
        previous[0] += total
        previous[1] += count
    return zones


def load(path):
    with path.open(encoding='utf-8-sig', newline='') as stream:
        return aggregate(csv.DictReader(stream))


def report(current, baseline):
    lines = ['# Inclusive Tracy CPU zone comparison', '',
             'Duplicate name/file/line rows are combined across threads. Times are inclusive;',
             'nested zones overlap. Totals are NOT CPU utilization or exclusive bottleneck shares.',
             'Per-call comparisons do not control workload, capture duration, or thread mix.', '',
             '| Zone (source) | Calls | Total ms | Mean us | Baseline mean us | Ratio |',
             '| --- | ---: | ---: | ---: | ---: | ---: |']
    for key, (total, count) in sorted(current.items(), key=lambda item: item[1][0], reverse=True):
        mean = total / count / 1000 if count else None
        old_total, old_count = baseline.get(key, (0, 0))
        old_mean = old_total / old_count / 1000 if old_count else None
        ratio = mean / old_mean if mean is not None and old_mean else None
        fmt = lambda value: f'{value:.3f}' if value is not None else 'n/a'
        name, source, line = key
        lines.append(f'| {name} ({source}:{line}) | {count} | {total / 1e6:.3f} | '
                     f'{fmt(mean)} | {fmt(old_mean)} | {fmt(ratio)} |')
    return '\n'.join(lines)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--current', required=True, type=Path)
    parser.add_argument('--baseline', type=Path)
    args = parser.parse_args()
    print(report(load(args.current), load(args.baseline) if args.baseline else {}))
