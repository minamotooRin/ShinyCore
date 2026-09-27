#!/usr/bin/env python3
"""Exercise the public project/package CLIs, optionally against a real engine."""

from __future__ import annotations

import json
import hashlib
import os
from pathlib import Path
import platform
import plistlib
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile


ROOT = Path(__file__).resolve().parents[1]
BINARY: Path | None = None


def layout(destination: Path) -> tuple[Path, Path]:
    if platform.system() == "Darwin":
        contents = destination / "ShinyCore.app" / "Contents"
        return contents / "MacOS" / "shiny", contents / "Resources"
    return destination / ("shiny.exe" if os.name == "nt" else "shiny"), destination


class ToolTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="shiny-tools-")
        self.base = Path(self.temporary.name)
        self.addCleanup(self.temporary.cleanup)

    def cli(self, script: Path, *arguments: object, ok: bool = True) -> subprocess.CompletedProcess[str]:
        result = subprocess.run(
            [sys.executable, str(script), *(str(arg) for arg in arguments)],
            cwd=self.base, text=True, capture_output=True, timeout=60,
        )
        if ok:
            self.assertEqual(result.returncode, 0, result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout)
        return result

    def fixture_checkout(self) -> tuple[Path, Path]:
        if BINARY is None:
            self.skipTest("package validation requires a real engine; pass its path")
        root = self.base / "engine fixture"
        for directory in ("tools", "docs", "licenses", "examples/lantern/replays", "examples/input"):
            (root / directory).mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / "tools" / "package.py", root / "tools" / "package.py")
        shutil.copy2(ROOT / "tools" / "runtime_deps.py", root / "tools" / "runtime_deps.py")
        shutil.copy2(ROOT / "tools" / "package_content.py", root / "tools" / "package_content.py")
        shutil.copy2(ROOT / "tools" / "sdk.py", root / "tools" / "sdk.py")
        for name in ("LICENSE", "THIRD_PARTY.md", "docs/api.lua", "docs/llm-guide.md", "docs/input.md", "licenses/Lua.txt"):
            (root / name).write_text(f"fixture {name}\n", encoding="utf-8")
        (root / "examples" / "lantern" / "main.lua").write_text("return {}\n", encoding="utf-8")
        (root / "examples" / "input" / "main.lua").write_text("return {}\n", encoding="utf-8")
        binary = root / BINARY.name
        shutil.copy2(BINARY, binary)
        return root, binary

    def test_new_game_creates_complete_project_and_refuses_overwrite(self) -> None:
        destination = self.base / "a game with spaces"
        self.cli(ROOT / "tools" / "new_game.py", destination)
        self.assertEqual(
            {path.name for path in destination.iterdir()},
            {"main.lua", "project.lua", "package.json", "shiny-sdk.json", "game", "rooms", "smoke.jsonl", "README.md", "AGENTS.md", ".luarc.json", "lib", "docs"},
        )
        config = json.loads((destination / ".luarc.json").read_text(encoding="utf-8"))
        self.assertEqual(config["runtime.version"], "Lua 5.4")
        self.assertEqual(config["workspace.library"], ["docs/api.lua", "lib/shiny"])
        self.assertIn("docs/api.lua", (destination / "AGENTS.md").read_text(encoding="utf-8"))
        self.assertIn("shiny --check-all .", (destination / "AGENTS.md").read_text(encoding="utf-8"))
        self.assertNotIn(str(ROOT), (destination / "AGENTS.md").read_text(encoding="utf-8"))
        self.assertNotIn(str(ROOT), (destination / "README.md").read_text(encoding="utf-8"))
        self.assertEqual(json.loads((destination / "smoke.jsonl").read_text(encoding="utf-8").splitlines()[0]), {"version": 3})
        self.assertIn("sc.input", (destination / "game/controller.lua").read_text(encoding="utf-8"))
        (destination / "main.lua").write_text("user changes must survive", encoding="utf-8")
        self.cli(ROOT / "tools" / "new_game.py", destination, ok=False)
        self.assertEqual((destination / "main.lua").read_text(encoding="utf-8"), "user changes must survive")
        occupied = self.base / "existing-file"
        occupied.write_text("keep", encoding="utf-8")
        self.cli(ROOT / "tools" / "new_game.py", occupied, ok=False)
        self.assertEqual(occupied.read_text(encoding="utf-8"), "keep")
        if os.name != "nt":
            dangling = self.base / "dangling"
            dangling.symlink_to(self.base / "missing")
            self.cli(ROOT / "tools" / "new_game.py", dangling, ok=False)
            self.assertTrue(dangling.is_symlink())

    def test_package_copies_resources_and_creates_runnable_archive_layout(self) -> None:
        root, binary = self.fixture_checkout()
        destination = self.base / "portable package"
        self.cli(root / "tools" / "package.py", binary, destination, "--no-strip")
        packaged_binary, resources = layout(destination)
        self.assertEqual(packaged_binary.read_bytes(), binary.read_bytes())
        report = json.loads((destination / "package-report.json").read_text(encoding="utf-8"))
        self.assertEqual(report["bytes"]["engine"], binary.stat().st_size)
        self.assertEqual(report["total_bytes"], sum(item["bytes"] for item in report["files"]))
        for item in report["files"]:
            data = (destination / item["path"]).read_bytes()
            self.assertEqual(len(data), item["bytes"])
            self.assertEqual(hashlib.sha256(data).hexdigest(), item["sha256"])
        for name in ("LICENSE", "THIRD_PARTY.md", "docs/api.lua", "docs/llm-guide.md", "docs/input.md", "licenses/Lua.txt", "examples/lantern/main.lua", "examples/input/main.lua"):
            self.assertEqual((resources / name).read_bytes(), (root / name).read_bytes())
        if os.name != "nt":
            self.assertTrue(os.access(packaged_binary, os.X_OK))
        if platform.system() == "Darwin":
            with (destination / "ShinyCore.app" / "Contents" / "Info.plist").open("rb") as file:
                self.assertEqual(plistlib.load(file)["CFBundleExecutable"], "launch")
            self.assertTrue(os.access(destination / "run-lantern.command", os.X_OK))
        with zipfile.ZipFile(str(destination) + ".zip") as archive:
            names = archive.namelist()
            entry = (Path(destination.name) / packaged_binary.relative_to(destination)).as_posix()
            self.assertIn(entry, names)
            if os.name != "nt":
                self.assertTrue((archive.getinfo(entry).external_attr >> 16) & 0o111)

    def test_package_refuses_existing_outputs_and_recursive_destination(self) -> None:
        root, binary = self.fixture_checkout()
        script = root / "tools" / "package.py"
        destination = self.base / "existing"
        destination.mkdir()
        marker = destination / "work.txt"
        marker.write_text("keep", encoding="utf-8")
        self.cli(script, binary, destination, "--no-strip", ok=False)
        self.assertEqual(marker.read_text(encoding="utf-8"), "keep")
        new_destination = self.base / "new"
        archive = Path(str(new_destination) + ".zip")
        archive.write_bytes(b"existing archive")
        self.cli(script, binary, new_destination, "--no-strip", ok=False)
        self.assertFalse(new_destination.exists())
        self.assertEqual(archive.read_bytes(), b"existing archive")
        nested = root / "examples" / "lantern" / "recursive"
        self.cli(script, binary, nested, "--no-strip", ok=False)
        self.assertFalse(nested.exists())
        missing = self.base / "missing-license-output"
        (root / "LICENSE").unlink()
        self.cli(script, binary, missing, "--no-strip", ok=False)
        self.assertFalse(missing.exists())

    def test_real_scaffold_and_relocated_package(self) -> None:
        if BINARY is None:
            self.skipTest("pass the built engine path to run actual relocation checks")
        project = self.base / "fresh game"
        self.cli(ROOT / "tools" / "new_game.py", project)

        def run_engine(binary: Path, *arguments: object) -> dict:
            result = subprocess.run(
                [str(binary), *(str(arg) for arg in arguments)], cwd=self.base,
                text=True, capture_output=True, timeout=60,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            return json.loads(result.stdout)

        checked = run_engine(BINARY, "--check-all", project)
        self.assertTrue(checked["ok"])
        initial = next(entity for entity in checked["entities"] if entity["tag"] == "player")
        jumped = run_engine(BINARY, "--headless", project, "--frames", 60, "--replay", project / "smoke.jsonl")
        jumping_player = next(entity for entity in jumped["entities"] if entity["tag"] == "player")
        self.assertGreater(jumping_player["x"], initial["x"])
        self.assertLess(jumping_player["y"], initial["y"])
        destination = self.base / "package to relocate"
        self.cli(ROOT / "tools" / "package.py", BINARY, destination, "--no-strip", "--no-zip")
        relocated = self.base / "unrelated location" / "moved package"
        relocated.parent.mkdir()
        shutil.move(str(destination), relocated)
        packaged_binary, resources = layout(relocated)
        report = json.loads((relocated / "package-report.json").read_text(encoding="utf-8"))
        self.assertTrue(report["native_dependencies"]["relocation_verified"])
        checked_package = run_engine(packaged_binary, "--check", resources / "examples" / "lantern")
        self.assertTrue(checked_package["ok"])
        if os.name != "nt":
            launcher = relocated / ("run-lantern.command" if platform.system() == "Darwin" else "run-lantern.sh")
            launched = run_engine(launcher, "--check")
            self.assertEqual(launched["hash"], checked_package["hash"])

    def test_network_package_matches_executable_capability(self) -> None:
        if BINARY is None:
            self.skipTest("requires a real engine")
        metadata = subprocess.run([str(BINARY), "--api"], capture_output=True, text=True, check=True, timeout=10)
        enabled = json.loads(metadata.stdout)["network"]["available"]
        destination = self.base / "network package"
        self.cli(ROOT / "tools" / "package.py", BINARY, destination,
                 "--no-strip", "--no-zip", "--with-network-examples", ok=enabled)
        if not enabled:
            self.assertFalse(destination.exists())
            return
        relocated = self.base / "network relocated"
        shutil.move(str(destination), relocated)
        binary, resources = layout(relocated)
        for path in ("examples/duet/main.lua", "docs/networking.md", "licenses/enet.txt"):
            self.assertEqual((resources / path).read_bytes(), (ROOT / path).read_bytes())
        result = subprocess.run([str(binary), "--check", str(resources / "examples" / "duet")],
                                cwd=self.base, capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(json.loads(result.stdout)["ok"])

    def test_custom_game_package_relocates_without_stock_example(self) -> None:
        if BINARY is None:
            self.skipTest("requires a real engine")
        destination = self.base / "custom package"
        self.cli(ROOT / "tools/package.py", BINARY, destination, "--project", ROOT / "examples/workshop", "--no-strip", "--no-zip")
        relocated = self.base / "moved custom package"
        shutil.move(str(destination), relocated)
        binary, resources = layout(relocated)
        self.assertFalse((resources / "examples/lantern").exists())
        self.assertTrue((resources / "game/assets/OFL.txt").is_file())
        result = subprocess.run([str(binary), "--headless", str(resources / "game"), "--frames", "200",
                                 "--replay", str(resources / "game/replays/smoke.txt"), "--save-dir", str(self.base / "saved game")],
                                cwd=self.base, capture_output=True, text=True, encoding="utf-8", timeout=20)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(json.loads(result.stdout)["ok"])
        self.assertTrue((self.base / "saved game/shiny.workshop/checkpoint.json").is_file())

    def test_explicit_package_keeps_late_modules_and_checks_staged_rooms(self) -> None:
        if BINARY is None:
            self.skipTest("requires a real engine")
        project = self.base / "authored"
        project.mkdir()
        (project / "project.lua").write_text('return {rooms={"main.lua","second.lua"}}', encoding="utf-8")
        (project / "main.lua").write_text('return {update=function() assert(require("late")==42) end}', encoding="utf-8")
        (project / "second.lua").write_text('return {}', encoding="utf-8")
        (project / "late.lua").write_text('return 42', encoding="utf-8")
        (project / "unused.lua").write_text('error("must not ship")', encoding="utf-8")
        manifest = {"format": 1, "scripts": ["main.lua"]}
        (project / "package.json").write_text(json.dumps(manifest), encoding="utf-8")
        destination = self.base / "selected package"
        failed = self.cli(ROOT / "tools/package.py", BINARY, destination, "--project", project,
                          "--no-strip", "--no-zip", ok=False)
        self.assertIn("packaged game validation failed", failed.stderr)
        self.assertFalse(destination.exists())
        manifest["scripts"].append("second.lua")
        (project / "package.json").write_text(json.dumps(manifest), encoding="utf-8")
        self.cli(ROOT / "tools/package.py", BINARY, destination, "--project", project, "--no-strip", "--no-zip")
        binary, resources = layout(destination)
        self.assertTrue((resources / "game/late.lua").is_file())
        self.assertFalse((resources / "game/unused.lua").exists())
        result = subprocess.run([str(binary), str(resources / "game"), "--headless", "--frames", "2"],
                                cwd=self.base, capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads((destination / "package-report.json").read_text(encoding="utf-8"))
        self.assertEqual(report["project_content"]["omitted_files"], ["unused.lua"])

    def test_package_symbols_and_unavailable_required_modules(self) -> None:
        if BINARY is None:
            self.skipTest("requires a real engine")
        project = self.base / "requirements"
        project.mkdir()
        (project / "main.lua").write_text('return {}', encoding="utf-8")
        api=json.loads(subprocess.check_output([str(BINARY),"--api"],encoding="utf-8"))
        unavailable=next((name for name,enabled in api["modules"].items() if not enabled),"unsupported_module")
        (project / "project.lua").write_text('return {modules={"'+unavailable+'"}}', encoding="utf-8")
        destination = self.base / "mismatch"
        result = self.cli(ROOT / "tools/package.py", BINARY, destination, "--project", project, ok=False)
        self.assertIn(unavailable, result.stderr)
        self.assertFalse(destination.exists())
        required="advanced_render" if api["modules"]["advanced_render"] else "settings"
        (project / "project.lua").write_text('return {modules={"'+required+'"}}', encoding="utf-8")
        symbols = self.base / "game.pdb"
        symbols.write_bytes(b"standalone symbol fixture")
        self.cli(ROOT / "tools/package.py", BINARY, destination, "--project", project,
                 "--symbols", symbols, "--no-strip", "--no-zip")
        report = json.loads((destination / "package-report.json").read_text(encoding="utf-8"))
        self.assertEqual(report["capabilities"],api["modules"])
        self.assertEqual(report["bytes"]["debug_symbols"], symbols.stat().st_size)
        self.assertEqual((destination / "debug-symbols/game.pdb").read_bytes(), symbols.read_bytes())


if __name__ == "__main__":
    if len(sys.argv) > 3:
        raise SystemExit("usage: test_tools.py [built-shiny [repository-root]]")
    if len(sys.argv) >= 2:
        BINARY = Path(sys.argv[1]).resolve()
    if len(sys.argv) == 3:
        ROOT = Path(sys.argv[2]).resolve()
    unittest.main(argv=[sys.argv[0]], verbosity=2)
