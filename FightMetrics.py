"""Fight-only sample statistics; HUD detection is not proof of visual correctness."""
import argparse
import csv
import json
import math
from pathlib import Path
import statistics


def number(value):
    try:
        result = float(value)
        return result if math.isfinite(result) and result >= 0 else None
    except (TypeError, ValueError):
        return None


def summarize(rows, watch):
    start = number(watch.get('first_fight_s')) if watch.get('fight_detected') else None
    fight = [row for row in rows if start is not None and
             number(row.get('elapsed_s')) is not None and float(row['elapsed_s']) >= start]
    values = lambda key: [v for row in fight if (v := number(row.get(key))) is not None]
    fps = values('fps')
    times = sorted(values('elapsed_s'))
    # Include detection-to-first-sample latency; elapsed coverage alone can hide gaps.
    gaps = [later - earlier for earlier, later in zip([start] + times, times)] if times else []
    duration = max(times) - start if times else 0
    # CPU seconds are cumulative across all process threads. Their slope is busy
    # core equivalents (1.0 = one fully busy core), not machine-wide utilization.
    ordered = sorted(fight, key=lambda row: float(row['elapsed_s']))
    cpu_cores = []
    for previous, current in zip(ordered, ordered[1:]):
        before, after = number(previous.get('proc_cpu_s')), number(current.get('proc_cpu_s'))
        elapsed = float(current['elapsed_s']) - float(previous['elapsed_s'])
        if before is not None and after is not None and elapsed > 0 and after >= before:
            cpu_cores.append((after - before) / elapsed)
    return {
        'fight_samples': len(fight),
        'fight_fps_samples': len(fps),
        'fight_observed_s': round(duration, 2),
        'fight_sample_span_s': round(max(times) - min(times), 2) if times else 0,
        'fight_max_sample_gap_s': round(max(gaps), 2) if gaps else 'n/a',
        'fight_120s_observed': duration >= 120,
        'fight_median_fps': statistics.median(fps) if fps else 'n/a',
        'fight_min_fps': min(fps) if fps else 'n/a',
        'fight_zero_fps_samples': fps.count(0),
        'fight_cpu_intervals': len(cpu_cores),
        'fight_median_cpu_busy_cores': round(statistics.median(cpu_cores), 3)
        if cpu_cores else 'n/a',
        'fight_max_rss_mb': max(values('proc_rss_mb'), default='n/a'),
        # nvidia-smi reports the entire device, not emulator-exclusive allocations.
        'fight_max_device_vram_mb': max(values('gpu_mem_mb'), default='n/a'),
        'fight_median_gpu_util_pct': statistics.median(values('gpu_util_pct'))
        if values('gpu_util_pct') else 'n/a',
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--session', type=Path, required=True)
    args = parser.parse_args()
    with (args.session / 'metrics.csv').open(encoding='utf-8-sig', newline='') as stream:
        rows = list(csv.DictReader(stream))
    watch_path = args.session / 'watch.json'
    watch = json.loads(watch_path.read_text(encoding='utf-8-sig')) if watch_path.exists() else {}
    for key, value in summarize(rows, watch).items():
        print(f'{key}={value}')


if __name__ == '__main__':
    main()
