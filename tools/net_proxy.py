#!/usr/bin/env python3
"""Bounded development-only IPv4 UDP fault proxy; no game protocol knowledge."""
import argparse
from dataclasses import dataclass
import heapq
import ipaddress
import json
import math
from pathlib import Path
import random
import selectors
import socket
import time


@dataclass(frozen=True)
class Policy:
    rtt_ms: float = 150.0
    jitter_ms: float = 30.0
    loss: float = .05
    duplicate: float = .01
    seed: int = 1
    max_clients: int = 16
    max_packets: int = 4096
    max_bytes: int = 8 * 1024 * 1024

    def __post_init__(self):
        for name in ('rtt_ms', 'jitter_ms', 'loss', 'duplicate'):
            if not math.isfinite(getattr(self, name)):
                raise ValueError(f'{name} must be finite')
        if not 0 <= self.jitter_ms <= self.rtt_ms <= 60000:
            raise ValueError('require 0 <= jitter_ms <= rtt_ms <= 60000')
        if not 0 <= self.loss <= 1 or not 0 <= self.duplicate <= 1:
            raise ValueError('loss and duplicate must be probabilities in 0..1')
        for name, limit in (('max_clients', 256), ('max_packets', 65536), ('max_bytes', 64*1024*1024)):
            value = getattr(self, name)
            if type(value) is not int or not 1 <= value <= limit:
                raise ValueError(f'{name} must be an integer in 1..{limit}')


class Proxy:
    def __init__(self, target, listen=('127.0.0.1', 0), policy=Policy()):
        for address, port in (target, listen):
            ipaddress.IPv4Address(address)  # Numeric addresses only; no DNS dependency.
            if type(port) is not int or not 0 <= port <= 65535:
                raise ValueError('port must be in 0..65535')
        if target[1] == 0:
            raise ValueError('target port must be nonzero')
        self.policy, self.target = policy, target
        self.random = random.Random(policy.seed)
        self.selector = selectors.DefaultSelector()
        self.listener = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.clients, self.pending = {}, []
        self.sequence = self.pending_bytes = 0
        self.counters = {direction: dict(received=0, lost=0, duplicated=0,
                                        overflow=0, forwarded=0, send_errors=0)
                         for direction in ('up', 'down')}
        self.rejected_client_packets = self.peak_packets = self.peak_bytes = 0
        try:
            self.listener.bind(listen)
            self.listener.setblocking(False)
            self.selector.register(self.listener, selectors.EVENT_READ, None)
        except BaseException:
            self.close()
            raise

    @property
    def address(self):
        return self.listener.getsockname()

    def close(self):
        for upstream in self.clients.values():
            upstream.close()
        self.clients.clear()
        self.listener.close()
        self.selector.close()
        self.pending.clear()
        self.pending_bytes = 0

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()

    def schedule(self, data, output, address, direction, now):
        counter = self.counters[direction]
        counter['received'] += 1
        if self.random.random() < self.policy.loss:
            counter['lost'] += 1
            return
        duplicate = self.random.random() < self.policy.duplicate
        counter['duplicated'] += int(duplicate)
        for _ in range(1 + int(duplicate)):
            delay = (self.policy.rtt_ms + self.random.uniform(
                -self.policy.jitter_ms, self.policy.jitter_ms)) / 2000
            if len(self.pending) >= self.policy.max_packets or self.pending_bytes + len(data) > self.policy.max_bytes:
                counter['overflow'] += 1
                continue
            self.sequence += 1
            heapq.heappush(self.pending, (now+delay, self.sequence, output, address, data, direction))
            self.pending_bytes += len(data)
            self.peak_packets = max(self.peak_packets, len(self.pending))
            self.peak_bytes = max(self.peak_bytes, self.pending_bytes)

    def step(self, wait=.05):
        now = time.monotonic()
        timeout = min(wait, max(0, self.pending[0][0]-now)) if self.pending else wait
        for key, _ in self.selector.select(timeout):
            # Bound per-socket work so one sender cannot starve scheduled delivery.
            for _ in range(64):
                try:
                    data, source = key.fileobj.recvfrom(65535)
                except BlockingIOError:
                    break
                except ConnectionResetError:
                    break  # Windows reports ICMP port-unreachable on UDP receives.
                if key.data is None:
                    upstream = self.clients.get(source)
                    if upstream is None:
                        if len(self.clients) >= self.policy.max_clients:
                            self.rejected_client_packets += 1
                            continue
                        upstream = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                        try:
                            upstream.connect(self.target)
                            upstream.setblocking(False)
                            self.selector.register(upstream, selectors.EVENT_READ, source)
                        except BaseException:
                            upstream.close()
                            raise
                        self.clients[source] = upstream
                    self.schedule(data, upstream, self.target, 'up', time.monotonic())
                else:
                    # Connected upstream sockets accept datagrams from target only.
                    self.schedule(data, self.listener, key.data, 'down', time.monotonic())
        now = time.monotonic()
        for _ in range(256):
            if not self.pending or self.pending[0][0] > now:
                break
            _, _, output, address, data, direction = heapq.heappop(self.pending)
            self.pending_bytes -= len(data)
            try:
                output.sendto(data, address)
                self.counters[direction]['forwarded'] += 1
            except OSError:
                self.counters[direction]['send_errors'] += 1

    def stats(self):
        return dict(version=1, policy=vars(self.policy), clients=len(self.clients),
                    rejected_client_packets=self.rejected_client_packets, queued=len(self.pending),
                    queued_bytes=self.pending_bytes, peak_packets=self.peak_packets,
                    peak_bytes=self.peak_bytes, directions=self.counters)


def endpoint(text):
    try:
        address, port = text.rsplit(':', 1)
        ipaddress.IPv4Address(address)
        port = int(port)
        if not 0 <= port <= 65535:
            raise ValueError()
        return address, port
    except ValueError as error:
        raise argparse.ArgumentTypeError('expected numeric IPv4:port') from error


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target', type=endpoint, required=True)
    parser.add_argument('--listen', type=endpoint, default=('127.0.0.1', 0))
    for field in ('rtt_ms', 'jitter_ms', 'loss', 'duplicate', 'seed', 'max_clients', 'max_packets', 'max_bytes'):
        default = getattr(Policy(), field)
        parser.add_argument('--'+field.replace('_', '-'), type=type(default), default=default)
    parser.add_argument('--duration', type=float, default=0, help='seconds; 0 runs until Ctrl+C')
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    try:
        if not math.isfinite(args.duration) or args.duration < 0:
            raise ValueError('duration must be finite and nonnegative')
        policy = Policy(**{key: getattr(args, key) for key in Policy.__dataclass_fields__})
        with Proxy(args.target, args.listen, policy) as proxy:
            print(json.dumps(dict(event='ready', listen=proxy.address, target=args.target)), flush=True)
            deadline = time.monotonic()+args.duration if args.duration else math.inf
            try:
                while time.monotonic() < deadline:
                    proxy.step(min(.05, max(0, deadline-time.monotonic())))
            except KeyboardInterrupt:
                pass
            report = json.dumps(proxy.stats(), indent=2)+'\n'
            if args.report:
                args.report.write_text(report, encoding='utf-8')
            print(report, flush=True)
    except (OSError, ValueError) as error:
        parser.exit(1, f'net_proxy: {error}\n')


if __name__ == '__main__':
    main()
