#!/usr/bin/env python3
"""Real-process UDP verification: python3 network_integration.py BINARY ROOT."""
import json
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
import unittest


if len(sys.argv) != 3:
    raise SystemExit(__doc__)
BINARY, ROOT = (Path(argument).resolve() for argument in sys.argv[1:])
sys.argv[1:] = []
SOURCE = (ROOT / "examples/duet/main.lua").read_text(encoding="utf-8")
REPLAYS = ROOT / "examples/duet/replays"


class Multiplayer(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="shiny-network-")
        self.addCleanup(temporary.cleanup)
        self.directory = Path(temporary.name)
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
            probe.bind(("127.0.0.1", 0))
            self.port = probe.getsockname()[1]
        self.source = SOURCE.replace("local PORT = 7777", f"local PORT = {self.port}", 1)

    def project(self, name, source=None):
        project = self.directory / name
        project.mkdir()
        (project / "main.lua").write_text(source or self.source, encoding="utf-8")
        return project

    def command(self, project, replay, frames):
        return [str(BINARY), "--headless", "--realtime", str(project),
                "--frames", str(frames), "--replay", str(REPLAYS / replay)]

    @staticmethod
    def stop(process):
        if process.poll() is None:
            process.kill()
        process.communicate()

    def pair(self, guest_source=None, host_frames=270, guest_frames=240):
        host_project = self.project("host")
        guest_project = self.project("guest", guest_source)
        log_path = self.directory / "host.log"
        with log_path.open("w", encoding="utf-8") as log:
            host = subprocess.Popen(self.command(host_project, "host.txt", host_frames),
                                    stdout=subprocess.PIPE, stderr=log, text=True, encoding="utf-8")
            self.addCleanup(self.stop, host)
            deadline = time.monotonic() + 5
            while f"DUET listening {self.port}" not in log_path.read_text(encoding="utf-8"):
                if host.poll() is not None or time.monotonic() > deadline:
                    self.fail("Host did not start: " + log_path.read_text(encoding="utf-8"))
                time.sleep(.01)
            guest = subprocess.run(self.command(guest_project, "join.txt", guest_frames),
                                   capture_output=True, text=True, encoding="utf-8", timeout=15)
            host_stdout, _ = host.communicate(timeout=15)
        host_log = log_path.read_text(encoding="utf-8")
        self.assertEqual(host.returncode, 0, host_log)
        self.assertEqual(guest.returncode, 0, guest.stderr)
        self.assertIn("DUET host ready", host_log)
        self.assertIn("DUET guest ready", guest.stderr)
        host_state, guest_state = json.loads(host_stdout), json.loads(guest.stdout)
        self.assertTrue(host_state["ok"] and guest_state["ok"])
        return host_state, guest_state, host_log, guest.stderr

    @staticmethod
    def entities(state):
        return {entity["tag"]: entity for entity in state["entities"]}

    def test_check_never_opens_a_listener(self):
        # An already occupied port must not affect initial scene validation.
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as occupied:
            occupied.bind(("127.0.0.1", self.port))
            result = subprocess.run([str(BINARY), "--check", str(self.project("check"))],
                                    capture_output=True, text=True, encoding="utf-8", timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn("DUET listening", result.stderr)
        self.assertEqual(json.loads(result.stdout)["frames"], 0)

    def test_named_session_survives_room_and_paused_simulation(self):
        host_project = self.project('named-host', '''local h
return {update=function()
    if not h then
        h=assert(sc.net.host('127.0.0.1',PORT)); assert(h:persist('coop'))
        sc.log('named listening'); sc.app.pause(true)
    end
    while true do
        local e,err=h:poll(); assert(not err); if not e then break end
        if e.type=='connect' then
            sc.state.set('peer',e.peer); sc.state.set('port',h:port())
            assert(h:send(e.peer,'hello')); assert(h:flush()); sc.scene('next.lua'); break
        end
    end
end}'''.replace('PORT', str(self.port)))
        (host_project/'project.lua').write_text(
            'return {rooms={"main.lua","next.lua"}}', encoding='utf-8')
        (host_project/'next.lua').write_text('''local h
return {init=function()
    h=assert(sc.net.bind('coop')); assert(h:port()==sc.state.get('port'))
    local ok,err=pcall(h.poll,h); assert(not ok and err:find('candidate initialization'))
    assert(h:stats().open)
end,update=function()
    sc.app.pause(true)
    while true do
        local e,err=h:poll(); assert(not err); if not e then break end
        if e.type=='receive' then
            assert(e.peer==sc.state.get('peer') and e.data=='ack')
            assert(h:send(e.peer,'done')); assert(h:flush())
            sc.log('named restored'); sc.app.quit()
        end
    end
end}''', encoding='utf-8')
        guest_project = self.project('named-guest', '''local h
return {update=function()
    if not h then h=assert(sc.net.join('127.0.0.1',PORT)); assert(h:persist('coop')) end
    while true do
        local e,err=h:poll(); assert(not err); if not e then break end
        if e.type=='receive' then
            if e.data=='hello' then assert(h:send(e.peer,'ack')); assert(h:flush())
            elseif e.data=='done' then sc.log('named complete'); sc.app.quit()
            else error('unexpected packet') end
        end
    end
end}'''.replace('PORT', str(self.port)))
        log_path = self.directory/'named.log'
        with log_path.open('w', encoding='utf-8') as log:
            host = subprocess.Popen(self.command(host_project, 'host.txt', 240),
                                    stdout=subprocess.PIPE, stderr=log, text=True, encoding='utf-8')
            self.addCleanup(self.stop, host)
            deadline = time.monotonic()+5
            while 'named listening' not in log_path.read_text(encoding='utf-8'):
                if host.poll() is not None or time.monotonic()>deadline:
                    self.fail('named host did not start: '+log_path.read_text(encoding='utf-8'))
                time.sleep(.01)
            guest = subprocess.run(self.command(guest_project, 'join.txt', 240),
                                   capture_output=True, text=True, encoding='utf-8', timeout=10)
            host.communicate(timeout=10)
        host_log = log_path.read_text(encoding='utf-8')
        self.assertEqual(host.returncode, 0, host_log)
        self.assertEqual(guest.returncode, 0, guest.stderr)
        self.assertIn('named restored', host_log)
        self.assertIn('named complete', guest.stderr)

    def test_two_processes_share_authoritative_gameplay(self):
        started = time.monotonic()
        host, guest, host_log, guest_log = self.pair()
        self.assertGreater(time.monotonic() - started, 3.5, "--realtime must pace real peers")
        self.assertIn("DUET host applied remote input", host_log)
        self.assertIn("DUET guest received snapshot", guest_log)
        self.assertIn("DUET complete", host_log)
        self.assertIn("DUET complete", guest_log)
        for state in (host, guest):
            entities = self.entities(state)
            self.assertGreater(entities["host"]["x"], 140)
            self.assertLess(entities["guest"]["x"], 240)
            self.assertAlmostEqual(entities["guest"]["y"], 112, delta=.2)
        # Shutdown delivery and client smoothing depend on packet arrival time;
        # identical hashes or exact final coordinates are not a network contract.

    def test_host_rejects_out_of_range_remote_input(self):
        marker = 'sent, x, y), "state")'
        self.assertEqual(self.source.count(marker), 1)
        malicious = self.source.replace(marker, 'sent, 2, y), "state")')
        host, _, host_log, _ = self.pair(malicious, host_frames=90, guest_frames=60)
        self.assertIn("DUET rejected invalid protocol packet", host_log)
        self.assertNotIn("DUET host applied remote input", host_log)
        self.assertEqual(self.entities(host)["guest"]["x"], 296)

    def test_missing_input_expires_instead_of_moving_forever(self):
        marker = 'if sc.tick() % 3 == 0 then\n            sent = (sent + 1) & 0xffffffff\n            send(packet(INPUT'
        self.assertEqual(self.source.count(marker), 1)
        silent = self.source.replace(marker, marker.replace("% 3 == 0", "% 3 == 0 and sc.tick() < 45"))
        host, _, host_log, _ = self.pair(silent, host_frames=150, guest_frames=120)
        self.assertIn("DUET host applied remote input", host_log)
        # The last packet still asks to move left. Its 250 ms lifetime, rather
        # than a later stop packet, must bound displacement on the host.
        x = self.entities(host)["guest"]["x"]
        self.assertGreater(x, 250)
        self.assertLess(x, 275)


if __name__ == "__main__":
    unittest.main()
