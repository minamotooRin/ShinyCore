#!/usr/bin/env python3
"""Exercise the shipped CLI, actual game, and useful failure diagnostics.

Usage: python3 tests/integration.py /absolute/path/shiny /repository/root
No display, audio device, Python packages, or writable repository are required.
"""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


if len(sys.argv) != 3:
    raise SystemExit(__doc__)
BINARY = Path(sys.argv[1]).resolve()
ROOT = Path(sys.argv[2]).resolve()
GAME = ROOT / "examples" / "lantern"
REPLAY = GAME / "replays" / "tour.txt"
# unittest should not interpret the binary and repository paths as test names.
sys.argv[1:] = []


class CliWorkflow(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="shiny-integration-")
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)

    def invoke(self, *arguments, success=True):
        result = subprocess.run(
            [str(BINARY), *(str(argument) for argument in arguments)],
            cwd=self.directory,
            capture_output=True,
            text=True,
            encoding="utf-8",
            timeout=15,
            check=False,
        )
        if success:
            self.assertEqual(result.returncode, 0, result.stderr or result.stdout)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout)
        return result

    def state(self, *arguments):
        result = self.invoke(*arguments)
        state = json.loads(result.stdout)
        self.assertIs(state["ok"], True)
        return state

    def failure(self, *arguments, code, contains=None):
        result = self.invoke(*arguments, success=False)
        self.assertEqual(result.stdout, "", "A failed run must not emit a successful snapshot")
        diagnostic = json.loads(result.stderr.splitlines()[-1])
        self.assertIs(diagnostic["ok"], False)
        self.assertEqual(diagnostic["code"], code)
        if contains:
            self.assertIn(contains, diagnostic["error"])
        return diagnostic

    def project(self, source):
        project = self.directory / "game with spaces"
        project.mkdir(exist_ok=True)
        (project / "main.lua").write_text(source, encoding="utf-8")
        return project

    @staticmethod
    def tagged(state):
        return {entity["tag"]: entity for entity in state["entities"] if entity["tag"]}

    def tour(self, frames, seed=42):
        return self.state(
            "--headless", GAME, "--frames", frames, "--seed", seed, "--replay", REPLAY
        )

    def test_discovery_and_argument_errors(self):
        api = json.loads(self.invoke("--api").stdout)
        self.assertEqual(api["engine"], "ShinyCore")
        self.assertEqual(api["fixed_hz"], 60)
        names = {function["name"] for function in api["functions"]}
        self.assertTrue({"sc.spawn", "sc.set", "sc.scene", "sc.pressed"} <= names)
        self.assertIn("--headless", self.invoke("--help").stdout)
        self.assertTrue(self.invoke("--version").stdout.strip())
        for arguments in [("--unknown",), ("--frames",), ("--frames", "-1"), ("--seed", "0")]:
            with self.subTest(arguments=arguments):
                self.failure(*arguments, code="arguments")

    def test_check_shipped_project_from_another_working_directory(self):
        state = self.state("--check", GAME)
        self.assertEqual(state["frames"], 0)
        self.assertEqual(state["scene"], "main.lua")
        tags = self.tagged(state)
        self.assertTrue({"keeper", "door", "wisp_1", "wisp_2", "wisp_3"} <= tags.keys())

    def test_actual_collection_and_room_round_trip(self):
        # These assertions verify authored behavior, not an opaque hash literal.
        for frames, remaining in [(150, {"wisp_2", "wisp_3"}), (220, {"wisp_3"}), (330, set())]:
            with self.subTest(frames=frames):
                state = self.tour(frames)
                self.assertEqual(state["scene"], "main.lua")
                self.assertEqual(
                    {tag for tag in self.tagged(state) if tag.startswith("wisp_")}, remaining
                )
        archive = self.tour(430)
        self.assertEqual(archive["frames"], 430)
        self.assertEqual(archive["scene"], "rooms/archive.lua")
        self.assertIn("memory", self.tagged(archive))
        keeper = self.tagged(archive)["keeper"]
        self.assertAlmostEqual(keeper["x"], 63, places=3)
        self.assertAlmostEqual(keeper["y"], 166, places=3)
        self.assertIs(keeper["grounded"], True)
        self.assertLess(archive["tick"], archive["frames"])
        returned = self.tour(480)
        self.assertEqual(returned["scene"], "main.lua")
        tags = self.tagged(returned)
        self.assertTrue({"wisp_1", "wisp_2", "wisp_3"} <= tags.keys())
        self.assertAlmostEqual(tags["keeper"]["x"], 40, places=3)
        self.assertIs(tags["keeper"]["grounded"], True)

    def test_repeated_seed_and_replay_are_deterministic(self):
        first = self.tour(430, seed=123)
        second = self.tour(430, seed=123)
        different = self.tour(430, seed=456)
        self.assertEqual(first, second)
        self.assertNotEqual(first["rng"], different["rng"])
        self.assertNotEqual(first["hash"], different["hash"])
        self.assertEqual(first["scene"], different["scene"])
        self.assertEqual(self.tagged(first)["keeper"], self.tagged(different)["keeper"])

    def test_replay_holds_actions_and_emits_one_press_edge(self):
        project = self.project("""
local probe
return {
  init = function() probe = sc.spawn({tag='probe', x=0, y=0, w=1, h=1}) end,
  update = function()
    local p = sc.get(probe)
    sc.set(probe, {
      x=p.x + (sc.down('right') and 1 or 0),
      y=p.y + (sc.pressed('jump') and 1 or 0),
    })
  end,
}
""")
        replay = self.directory / "input.txt"
        replay.write_text("# initial input is empty\n2 18\n5 2 # release jump\n8 0\n", encoding="utf-8")
        state = self.state("--headless", project, "--frames", 10, "--replay", replay)
        probe = self.tagged(state)["probe"]
        self.assertEqual(probe["x"], 6)
        self.assertEqual(probe["y"], 1)

    def test_snapshot_file_matches_stdout_and_escapes_text(self):
        message = 'line one\n"second" \\ 灯'
        project = self.project("return {init=function() sc.message([[" + message + "]]) end}")
        output = self.directory / "saved state.json"
        result = self.invoke("--headless", project, "--frames", 0, "--snapshot", output)
        self.assertEqual(output.read_text(encoding="utf-8"), result.stdout)
        self.assertEqual(json.loads(result.stdout)["message"], message)
        self.failure(
            "--headless", project, "--frames", 0, "--snapshot", self.directory / "missing" / "state.json",
            code="snapshot",
        )

    def test_invalid_projects_and_missing_assets_have_actionable_errors(self):
        self.failure("--check", self.directory / "absent", code="scene", contains="main.lua")
        for source, expected in [
            ("return {", "main.lua"),
            ("return {widht=384}", "widht"),
            ("return {entities={{sprite='missing.png'}}}", "missing.png"),
        ]:
            with self.subTest(source=source):
                self.failure("--check", self.project(source), code="scene", contains=expected)

    def test_update_failure_has_a_nonzero_exit_and_script_location(self):
        project = self.project("""
return {update=function()
  if sc.tick() == 1 then error('intentional update failure') end
end}
""")
        self.state("--check", project)  # Validation does not execute game updates.
        diagnostic = self.failure(
            "--headless", project, "--frames", 3, code="script", contains="intentional update failure"
        )
        self.assertIn("main.lua:", diagnostic["error"])

    def test_malformed_replays_are_rejected_with_the_line_number(self):
        project = self.project("return {}")
        replay = self.directory / "bad.txt"
        for content, line in [("0 0\n0 2\n", 2), ("0 64\n", 1), ("0 2 extra\n", 1)]:
            with self.subTest(content=content):
                replay.write_text(content, encoding="utf-8")
                self.failure(
                    "--headless", project, "--frames", 2, "--replay", replay,
                    code="replay", contains=f"bad.txt:{line}:",
                )


if __name__ == "__main__":
    unittest.main(verbosity=2)
