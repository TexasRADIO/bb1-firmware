#!/usr/bin/env python3
"""Report echo/spring costs from valid, correlated peak half-buffer snapshots."""
import argparse
import json
from pathlib import Path


def peak_halves(lines):
    window = None
    stage = None
    fields = {
        'half_sequence', 'half_valid', 'half_total_ticks', 'half_budget_ticks',
        'half_frames', 'half_worker_wait_ticks', 'half_worker_run_ticks',
    }
    for line in lines:
        parts = line.split()
        if len(parts) != 2:
            continue
        key, value = parts
        if key == 'profile_half':
            window = {} if value == '1' else None
            stage = None
        elif window is not None:
            if key == 'half_stage':
                stage = value
            elif key == 'half_stage_ticks' and stage in ('3', '4'):
                window['echo_ticks' if stage == '3' else 'spring_ticks'] = int(value, 0)
            elif key in fields:
                window[key] = int(value, 0)
            if fields | {'echo_ticks', 'spring_ticks'} <= window.keys():
                if window['half_valid'] == 1 and window['half_budget_ticks'] > 0 and window['half_frames'] > 0:
                    yield window
                window = None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    args = parser.parse_args()
    windows = list(peak_halves(args.capture.read_text().splitlines()))
    if not windows:
        parser.error('capture has no complete valid peak half-buffer snapshot')
    peak = max(windows, key=lambda w: w['half_total_ticks'] / w['half_budget_ticks'])
    budget = peak['half_budget_ticks']
    print(json.dumps({
        'valid_peak_snapshots': len(windows),
        'highest_budget_fraction': peak['half_total_ticks'] / budget,
        'echo_budget_fraction': peak['echo_ticks'] / budget,
        'spring_budget_fraction': peak['spring_ticks'] / budget,
        'worker_wait_budget_fraction': peak['half_worker_wait_ticks'] / budget,
        'snapshot': peak,
    }, indent=2))


if __name__ == '__main__':
    main()
