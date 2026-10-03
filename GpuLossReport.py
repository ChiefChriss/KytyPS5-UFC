"""Classify a device loss from the console log and NVIDIA/Display driver events."""
import argparse
from collections import Counter
import csv
from pathlib import Path
import re

# nvlddmkm 153 and Display 4101 are Windows TDR timeout/recovery events.
TIMEOUT_EVENTS = {('nvlddmkm', 153), ('Display', 4101)}
MEMORY_FAULT_TYPES = ('ReadInvalid', 'WriteInvalid', 'ExecuteInvalid')
BREADCRUMBS = re.compile(r'GPU breadcrumbs: recorded=(\d+) gpu_started=(\d+) '
                         r'gpu_completed=(\d+) suspects=(\d+)')
SUSPECT = re.compile(r'GPU breadcrumb suspect: seq=(\d+) op=(\w+).*?ps=(0x[0-9a-f]+)'
                     r'.*?shader_hash=([0-9a-f]+)')
FAULT_ADDRESS = re.compile(r'address type=(\w+)')


def summarize(console_lines, events):
    device_lost = any('DeviceLost' in line for line in console_lines)
    driver = [(event['provider'], int(event['id'])) for event in events
              if event.get('provider') and str(event.get('id', '')).isdigit()]
    fault_types = [match.group(1) for line in console_lines
                   if (match := FAULT_ADDRESS.search(line))]
    crumbs = [match for line in console_lines if (match := BREADCRUMBS.search(line))]
    suspects = [match for line in console_lines if (match := SUSPECT.search(line))]
    if not device_lost:
        loss_class = 'driver_event_without_loss' if driver else 'none'
    elif any(key in TIMEOUT_EVENTS for key in driver):
        loss_class = 'timeout'
    elif any(kind.endswith(MEMORY_FAULT_TYPES) for kind in fault_types):
        loss_class = 'memory_fault'
    elif driver:
        loss_class = 'driver_error'
    else:
        loss_class = 'unknown'
    ops = Counter(match.group(2) for match in suspects)
    hashes = sorted({match.group(4) for match in suspects if int(match.group(4), 16)})
    return {
        'gpu_device_lost': device_lost,
        'gpu_loss_class': loss_class,
        'gpu_driver_events': ','.join(f'{provider}:{event_id}' for provider, event_id in driver)
        or 'none',
        'gpu_fault_address_types': ','.join(fault_types) or 'none',
        'gpu_breadcrumb_range': (f'{crumbs[-1].group(3)}-{crumbs[-1].group(2)}'
                                 f' of {crumbs[-1].group(1)}') if crumbs else 'n/a',
        'gpu_breadcrumb_suspects': crumbs[-1].group(4) if crumbs else 'n/a',
        'gpu_breadcrumb_ops': ','.join(f'{op}x{count}' for op, count in ops.most_common())
        or 'n/a',
        'gpu_breadcrumb_compute_hashes': ','.join(hashes) or 'n/a',
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--session', type=Path, required=True)
    args = parser.parse_args()
    console = args.session / 'console.log'
    lines = console.read_text(encoding='utf-8', errors='replace').splitlines() \
        if console.exists() else []
    events_path = args.session / 'gpu-driver-events.csv'
    events = []
    if events_path.exists():
        with events_path.open(encoding='utf-8-sig', newline='') as stream:
            events = list(csv.DictReader(stream))
    for key, value in summarize(lines, events).items():
        print(f'{key}={value}')


if __name__ == '__main__':
    main()
