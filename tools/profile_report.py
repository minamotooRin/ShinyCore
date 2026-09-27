#!/usr/bin/env python3
"""Summarize versioned ShinyCore profiles; reject incomplete or insufficient measurements."""
import argparse
import json
import math
from pathlib import Path


COUNTS = ('entities', 'particles', 'projectiles', 'draw_commands')


def number(value, field, integer=False):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or value < 0:
        raise ValueError(f'{field}: expected a finite nonnegative number')
    if integer and int(value) != value:
        raise ValueError(f'{field}: expected an integer')
    return int(value) if integer else value


def load(path):
    with path.open(encoding='utf-8') as source:
        records = [json.loads(line) for line in source if line.strip()]
    if len(records) < 2 or any(not isinstance(r, dict) for r in records):
        raise ValueError('expected header, frames and end records')
    header, end = records[0], records[-1]
    if header.get('type') != 'header' or header.get('version') != 1:
        raise ValueError('unsupported profile version')
    if not isinstance(header.get('graphics'), bool) or not isinstance(header.get('trace_or_record'), bool):
        raise ValueError('header flags must be booleans')
    if header.get('gpu_timer') not in ('headless', 'unsupported', 'gl_time_elapsed'):
        raise ValueError('unknown GPU timer')
    if end.get('type') != 'end':
        raise ValueError('incomplete profile: missing end record')
    frames, gpu = [], {}
    for record in records[1:-1]:
        if record.get('type') == 'frame':
            number(record.get('frame'), 'frame', integer=True)
            if record.get('frame') != len(frames):
                raise ValueError('frame sequence must start at zero and be contiguous')
            for field in ('elapsed_ms', 'wall_ms', 'cpu_ms', 'wait_ms', 'diagnostic_ms'):
                number(record.get(field), field)
            for field in ('tick', 'steps', *COUNTS):
                number(record.get(field), field, integer=True)
            for field in ('resident_bytes', 'peak_resident_bytes', 'projectile_hits_total'):
                if record.get(field) is not None:
                    number(record[field], field, integer=True)
            phases = record.get('phases')
            if not isinstance(phases, dict):
                raise ValueError('phases: expected an object')
            for field, value in phases.items():
                number(value, field)
            if frames and (record['elapsed_ms'] < frames[-1]['elapsed_ms'] or record['tick'] < frames[-1]['tick']):
                raise ValueError('frame time and tick must not decrease')
            if not isinstance(record.get('gpu_issued'), bool):
                raise ValueError('gpu_issued: expected a boolean')
            frames.append(record)
        elif record.get('type') == 'gpu':
            frame = number(record.get('frame'), 'gpu.frame', integer=True)
            if frame in gpu or frame >= len(frames) or not frames[frame]['gpu_issued']:
                raise ValueError('duplicate or unissued GPU sample')
            gpu[frame] = number(record.get('gpu_ms'), 'gpu_ms')
        else:
            raise ValueError('unknown record type')
    if end.get('frames') != len(frames) or end.get('gpu_samples') != len(gpu):
        raise ValueError('end counts do not match records')
    issued = sum(f['gpu_issued'] for f in frames)
    if end.get('gpu_pending') != issued - len(gpu):
        raise ValueError('GPU pending count does not match records')
    supported = header.get('gpu_timer') == 'gl_time_elapsed'
    if end.get('gpu_skipped') != (len(frames) - issued if supported else 0) or (issued and not supported):
        raise ValueError('GPU capability/count mismatch')
    return header, frames, gpu, end


def distribution(values):
    if not values:
        return None
    ordered = sorted(values)
    return {**{f'p{p}': ordered[max(0, math.ceil(len(ordered)*p/100)-1)] for p in (50, 95, 99)},
            'max': ordered[-1], 'samples': len(ordered)}


def summarize(path, args):
    header, frames, gpu, end = load(path)
    start, stop = args.warmup*1000, (args.warmup+args.duration)*1000
    selected = [f for f in frames if f['elapsed_ms'] >= start and (args.duration == 0 or f['elapsed_ms'] < stop)]
    errors = []
    if not selected:
        raise ValueError('no frames in the measurement interval')
    if args.duration and frames[-1]['elapsed_ms']+frames[-1]['wall_ms'] < stop:
        errors.append('recording does not cover the full measurement duration')
    cpu = distribution([f['cpu_ms'] for f in selected])
    measured_gpu = distribution([gpu[f['frame']] for f in selected if f['frame'] in gpu])
    missing_gpu = sum(f['frame'] not in gpu for f in selected)
    peaks = [f['peak_resident_bytes'] for f in selected if f.get('peak_resident_bytes') is not None]
    peak = max(peaks) if peaks else None
    if args.cpu_p99_max is not None and cpu['p99'] > args.cpu_p99_max:
        errors.append('CPU p99 exceeds limit')
    if args.cpu_max is not None and cpu['max'] > args.cpu_max:
        errors.append('CPU maximum exceeds limit')
    if args.gpu_p99_max is not None:
        if missing_gpu:
            errors.append(f'GPU samples missing for {missing_gpu} measured frames')
        if measured_gpu and measured_gpu['p99'] > args.gpu_p99_max:
            errors.append('GPU p99 exceeds limit')
    if args.rss_max_mib is not None:
        if len(peaks) != len(selected):
            errors.append('process peak resident memory unavailable')
        elif peak > args.rss_max_mib*1024*1024:
            errors.append('process peak resident memory exceeds limit')
    for field, minimum in args.minimum:
        if min(f[field] for f in selected) < minimum:
            errors.append(f'{field} fell below {minimum}')
    if args.min_hits_per_second is not None:
        # Check every complete wall-time second, rather than hiding idle spans in an average.
        anchor = selected[0]
        windows = 0
        for frame in selected[1:]:
            if frame.get('projectile_hits_total') is None or anchor.get('projectile_hits_total') is None:
                errors.append('projectile hit counter unavailable')
                break
            if frame['projectile_hits_total'] < anchor['projectile_hits_total']:
                errors.append('projectile hit counter reset during measurement')
                break
            seconds = (frame['elapsed_ms']-anchor['elapsed_ms'])/1000
            if seconds >= 1:
                windows += 1
                if (frame['projectile_hits_total']-anchor['projectile_hits_total'])/seconds < args.min_hits_per_second:
                    errors.append(f'hit rate below limit at frame {frame["frame"]}')
                    break
                anchor = frame
        if not windows:
            errors.append('no complete one-second hit-rate window')
    limits = any(getattr(args, field) is not None for field in
                 ('cpu_p99_max', 'gpu_p99_max', 'cpu_max', 'rss_max_mib', 'min_hits_per_second')) or args.minimum
    if limits and header.get('trace_or_record'):
        errors.append('disable trace/record for performance checks')
    return {'file': str(path), 'header': header, 'ok': not errors, 'errors': errors,
            'first_frame': selected[0]['frame'], 'last_frame': selected[-1]['frame'],
            'cpu_ms': cpu, 'gpu_ms': measured_gpu, 'gpu_missing': missing_gpu,
            'wait_ms': distribution([f['wait_ms'] for f in selected]),
            'phases': {field: distribution([f['phases'][field] for f in selected]) for field in selected[0]['phases']},
            'minimum_counts': {field: min(f[field] for f in selected) for field in COUNTS},
            'peak_resident_bytes': peak, 'end': end}


def nonnegative(text):
    try:
        return number(float(text), 'argument')
    except ValueError as error:
        raise argparse.ArgumentTypeError(str(error)) from error


def minimum(text):
    try:
        field, value = text.split('=', 1)
        if field not in COUNTS:
            raise ValueError(f'count must be one of {", ".join(COUNTS)}')
        return field, number(float(value), field, integer=True)
    except ValueError as error:
        raise argparse.ArgumentTypeError(str(error)) from error


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('profiles', nargs='+', type=Path)
    parser.add_argument('--warmup', type=nonnegative, default=30, help='wall seconds excluded at start (default 30)')
    parser.add_argument('--duration', type=nonnegative, default=180, help='wall seconds measured; zero uses all remaining frames')
    parser.add_argument('--runs', type=int, default=3, help='required independent profile files (default 3)')
    for flag in ('cpu-p99-max', 'gpu-p99-max', 'cpu-max', 'rss-max-mib', 'min-hits-per-second'):
        parser.add_argument('--'+flag, type=nonnegative)
    parser.add_argument('--minimum', action='append', type=minimum, default=[], metavar='COUNTER=N')
    args = parser.parse_args()
    if args.runs < 1:
        parser.error('--runs must be positive')
    errors, reports = [], []
    if len(args.profiles) != args.runs or len({p.resolve() for p in args.profiles}) != args.runs:
        errors.append(f'expected {args.runs} distinct profile files')
    for path in args.profiles:
        try:
            reports.append(summarize(path, args))
        except (OSError, ValueError, KeyError, TypeError) as error:
            errors.append(f'{path}: {error}')
    if reports and any(r['header'] != reports[0]['header'] for r in reports[1:]):
        errors.append('run headers differ')
    ok = not errors and all(r['ok'] for r in reports)
    print(json.dumps({'ok': ok, 'errors': errors, 'runs': reports,
                      'measurement': {k: v for k, v in vars(args).items() if k != 'profiles'}}, indent=2))
    return 0 if ok else 1


if __name__ == '__main__':
    raise SystemExit(main())
