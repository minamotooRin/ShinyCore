"""UDP fault injection contracts, socket isolation and native four-process traffic."""
import json
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
from net_proxy import Policy, Proxy

BINARY = Path(sys.argv.pop(1)).resolve() if len(sys.argv)>1 else None


class Faults(unittest.TestCase):
    def test_validation(self):
        for arguments in ({'loss': 1.1}, {'duplicate': -1}, {'rtt_ms': float('nan')},
                          {'jitter_ms': 151}, {'max_packets': 0}, {'max_bytes': 2**30}):
            with self.assertRaises(ValueError):
                Policy(**arguments)

    def test_seed_delay_loss_duplication_and_capacity(self):
        with Proxy(('127.0.0.1', 9)) as first, Proxy(('127.0.0.1', 9)) as second:
            for proxy in (first, second):
                for i in range(1000):
                    proxy.schedule(str(i).encode(), None, None, 'up', 10)
            self.assertEqual(first.pending, second.pending)
            counts = first.stats()['directions']['up']
            self.assertGreater(counts['lost'], 0)
            self.assertGreater(counts['duplicated'], 0)
            self.assertTrue(all(10.06 <= item[0] <= 10.09 for item in first.pending))
            self.assertEqual(len(first.pending), 1000-counts['lost']+counts['duplicated'])
        policy = Policy(loss=0, duplicate=1, max_packets=3, max_bytes=8)
        with Proxy(('127.0.0.1', 9), policy=policy) as proxy:
            for _ in range(4):
                proxy.schedule(b'abcd', None, None, 'up', 0)
            self.assertEqual(proxy.pending_bytes, 8)
            self.assertEqual(len(proxy.pending), 2)
            self.assertEqual(proxy.counters['up']['overflow'], 6)
        with Proxy(('127.0.0.1', 9), policy=Policy(loss=0, duplicate=0, max_packets=3)) as proxy:
            for _ in range(4):
                proxy.schedule(b'a', None, None, 'up', 0)
            self.assertEqual(len(proxy.pending), 3)
            self.assertEqual(proxy.counters['up']['overflow'], 1)

    def test_multiple_clients_have_isolated_upstream_ports(self):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as server:
            server.bind(('127.0.0.1', 0))
            server.setblocking(False)
            with Proxy(server.getsockname(), policy=Policy(rtt_ms=0, jitter_ms=0, loss=0, duplicate=0, max_clients=2)) as proxy:
                clients = [socket.socket(socket.AF_INET, socket.SOCK_DGRAM) for _ in range(3)]
                try:
                    for i, client in enumerate(clients):
                        client.setblocking(False)
                        client.sendto(str(i).encode(), proxy.address)
                    replies, ports = {}, set()
                    deadline = time.monotonic()+2
                    while len(replies)<2 and time.monotonic()<deadline:
                        proxy.step(.001)
                        try:
                            data, address = server.recvfrom(100)
                            ports.add(address[1]); server.sendto(b'echo'+data, address)
                        except BlockingIOError:
                            pass
                        for i, client in enumerate(clients):
                            try:
                                data, _ = client.recvfrom(100)
                                replies[i] = data
                            except BlockingIOError:
                                pass
                    self.assertEqual(replies, {0: b'echo0', 1: b'echo1'})
                    self.assertEqual(len(ports), 2)
                    self.assertEqual(proxy.rejected_client_packets, 1)
                finally:
                    for client in clients:
                        client.close()


@unittest.skipUnless(BINARY, 'pass a network-enabled executable for native traffic')
class NativeTraffic(unittest.TestCase):
    def test_four_process_reliable_order_under_faults(self):
        with tempfile.TemporaryDirectory(prefix='shiny-weak-net-') as temporary:
            root = Path(temporary)
            processes, logs = [], []
            def launch(command, name):
                log = (root/(name+'.log')).open('w', encoding='utf-8')
                logs.append(log)
                process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
                processes.append(process)
                return process
            def wait_line(name, text):
                deadline = time.monotonic()+5
                while text not in (root/(name+'.log')).read_text(encoding='utf-8'):
                    if time.monotonic()>deadline:
                        self.fail('missing '+text+' in '+(root/(name+'.log')).read_text(encoding='utf-8'))
                    time.sleep(.01)
            def game(name, source, frames):
                path = root/name; path.mkdir()
                (path/'main.lua').write_text(source, encoding='utf-8')
                return launch([str(BINARY), '--headless', '--realtime', str(path), '--frames', str(frames)], name)
            try:
                host = game('host', '''local h; local received={}; local complete=0
return {update=function()
    if not h then h=assert(sc.net.host('127.0.0.1',0,3)); assert(h:persist('test')); sc.log('PORT '..h:port()) end
    while true do
        local e,err=h:poll(); assert(not err); if not e then break end
        if e.type=='receive' then
            local n=tonumber(e.data); assert(n and n==(received[e.peer] or 0)+1)
            received[e.peer]=n; assert(h:send(e.peer,e.data))
            if n==80 then complete=complete+1; sc.log('COMPLETE '..complete) end
        end
    end
    assert(h:flush())
end}''', 480)
                wait_line('host', 'PORT ')
                import re
                port = int(re.search(r'PORT (\d+)', (root/'host.log').read_text(encoding='utf-8'))[1])
                report = root/'proxy.json'
                relay = launch([sys.executable, str(ROOT/'tools/net_proxy.py'), '--target', f'127.0.0.1:{port}',
                                '--duration', '9', '--report', str(report), '--seed', '734',
                                '--duplicate', '.03'], 'proxy')
                wait_line('proxy', '"event": "ready"')
                listen = json.loads((root/'proxy.log').read_text(encoding='utf-8').splitlines()[0])['listen'][1]
                clients = []
                for i in range(3):
                    clients.append(game('client'+str(i), '''local h; local peer; local sent=0; local received=0
return {update=function()
    if not h then h=assert(sc.net.join('127.0.0.1',PORT)); assert(h:persist('test')) end
    while true do
        local e,err=h:poll(); assert(not err); if not e then break end
        if e.type=='connect' then peer=e.peer
        elseif e.type=='receive' then
            received=received+1; assert(tonumber(e.data)==received)
            if received==80 then sc.log('COMPLETE'); sc.app.quit() end
        end
    end
    if peer and sent<80 and sc.tick()%3==0 then sent=sent+1; assert(h:send(peer,tostring(sent))) end
    assert(h:flush())
end}'''.replace('PORT', str(listen)), 450))
                for i, process in enumerate(clients):
                    self.assertEqual(process.wait(timeout=12), 0, (root/f'client{i}.log').read_text(encoding='utf-8'))
                    self.assertIn('COMPLETE', (root/f'client{i}.log').read_text(encoding='utf-8'))
                self.assertEqual(host.wait(timeout=12), 0, (root/'host.log').read_text(encoding='utf-8'))
                self.assertIn('COMPLETE 3', (root/'host.log').read_text(encoding='utf-8'))
                self.assertEqual(relay.wait(timeout=12), 0)
                stats = json.loads(report.read_text(encoding='utf-8'))
                print('UDP fault report: '+json.dumps(stats, sort_keys=True), flush=True)
                self.assertEqual(stats['clients'], 3)
                for direction in stats['directions'].values():
                    self.assertGreater(direction['lost'], 0)
                    self.assertGreater(direction['duplicated'], 0)
                    self.assertEqual(direction['overflow'], 0)
                    self.assertEqual(direction['send_errors'], 0)
            finally:
                for process in processes:
                    if process.poll() is None:
                        process.kill()
                    process.wait()
                for log in logs:
                    log.close()


if __name__ == '__main__':
    unittest.main()
