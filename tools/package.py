#!/usr/bin/env python3
"""Bundle a built ShinyCore executable, an authored game, documentation, and licenses."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import plistlib
import re
import shutil
import stat
import subprocess
import sys
import zipfile

from runtime_deps import audit as audit_runtimes, relocate as relocate_runtimes
from runtime_deps import verify_relocated
from package_content import closure as content_closure
from sdk import audit as audit_sdk


ROOT = Path(__file__).resolve().parents[1]
APP_NAME = "ShinyCore.app"
MAC_LAUNCH = '''#!/bin/sh
set -eu
cd "$(CDPATH= cd -P "$(dirname "$0")/../Resources" && pwd)"
exec ../MacOS/shiny examples/lantern "$@"
'''
MAC_COMMAND = '''#!/bin/sh
set -eu
PACKAGE_DIR=$(CDPATH= cd -P "$(dirname "$0")" && pwd)
exec "$PACKAGE_DIR/ShinyCore.app/Contents/MacOS/launch" "$@"
'''
UNIX_LAUNCH = '''#!/bin/sh
set -eu
cd "$(CDPATH= cd -P "$(dirname "$0")" && pwd)"
exec ./shiny examples/lantern "$@"
'''
WINDOWS_LAUNCH = r'''@echo off
setlocal
cd /d "%~dp0"
shiny.exe examples\lantern %*
exit /b %errorlevel%
'''


def game_executable_name(name: str, project: Path | None) -> str:
    if platform.system() != "Windows" or project is None:
        raise OSError("--launcher-exe requires --project and a Windows executable")
    stem = name[:-4] if name.lower().endswith(".exe") else name
    if (not stem or len(stem) > 64 or stem != stem.strip(" .")
            or any(ord(char) < 32 or char in '<>:"/\\|?*' for char in stem)
            or re.fullmatch(r"(?:con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\..*)?", stem, re.I)):
        raise OSError("--launcher-exe: invalid Windows executable name")
    return stem + ".exe"


def executable(path: Path, contents: str | None = None) -> None:
    if contents is not None:
        path.write_text(contents, encoding="utf-8")
    path.chmod(path.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


def preflight(binary: Path, destination: Path, make_zip: bool, with_network: bool = False, custom: bool = False) -> Path:
    archive = Path(str(destination) + ".zip")
    for path in (destination, archive) if make_zip else (destination,):
        if os.path.lexists(path):
            raise FileExistsError(f"output already exists; refusing to overwrite: {path}")
    if not binary.is_file():
        raise OSError(f"engine executable is missing: {binary}")
    files = ["LICENSE", "THIRD_PARTY.md", "docs/api.lua", "docs/llm-guide.md", "docs/input.md"]
    directories = ["licenses"] + ([] if custom else ["examples/lantern", "examples/input"])
    if with_network:
        files += ["docs/networking.md", "examples/duet/main.lua", "licenses/enet.txt"]
        directories += ["examples/duet"]
    for name in files:
        if not (ROOT / name).is_file():
            raise OSError(f"required package file is missing: {ROOT / name}")
    for name in directories:
        source = ROOT / name
        if not source.is_dir():
            raise OSError(f"required package directory is missing: {source}")
        if destination.resolve().is_relative_to(source.resolve()):
            raise OSError(f"destination must be outside packaged source directory: {source}")
    if not any((ROOT / "licenses").iterdir()):
        raise OSError("licenses/ is empty; include the dependency license texts before packaging")
    if not custom and not (ROOT / "examples" / "lantern" / "main.lua").is_file():
        raise OSError("Lantern's main.lua is missing")
    return archive


def write_report(destination: Path, binary: Path, metadata: dict, dependencies: dict, content: dict | None, sdk: dict | None) -> None:
    """Record shipped bytes, including a per-file inventory that can be audited after moving."""
    sizes = dict.fromkeys(("engine", "runtime_libraries", "resources", "standard_library", "game_code", "debug_symbols", "other"), 0)
    files = []
    for path in sorted(destination.rglob("*")):
        if not path.is_file():
            continue
        relative = path.relative_to(destination).as_posix()
        if path == binary:
            category = "engine"
        elif path.parent == binary.parent and path.name in dependencies["runtime_files"]:
            category = "runtime_libraries"
        elif "debug-symbols" in path.relative_to(destination).parts:
            category = "debug_symbols"
        elif "/lib/shiny/" in "/" + relative:
            category = "standard_library"
        elif "docs" in path.relative_to(destination).parts or path.suffix in (".md", ".txt") or path.name.startswith("."):
            category = "other"
        elif "/game/" in "/" + relative or "/examples/" in "/" + relative:
            category = "game_code" if path.suffix == ".lua" else "resources"
        else:
            category = "other"
        size = path.stat().st_size
        sizes[category] += size
        with path.open("rb") as source:
            digest = hashlib.file_digest(source, "sha256").hexdigest()
        files.append({"path": relative, "bytes": size, "category": category, "sha256": digest})
    report = {"format": 1, "engine_version": metadata["version"],
              "capabilities": metadata["modules"], "platform": platform.system(), "machine": platform.machine(),
              "bytes": sizes, "total_bytes": sum(sizes.values()), "files": files,
              "native_dependencies": dependencies,
              "project_content": content or {"mode": "conservative copy"},
              "lua_sdk": sdk,
              "notes": ["Totals exclude package-report.json and the ZIP container.",
                        "Debug-symbol bytes count supplied separate files; embedded symbols remain in engine bytes.",
                        "Explicit project package.json selects content; without it authored files are copied conservatively."]}
    (destination / "package-report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def copy_resources(destination: Path, with_network: bool = False, custom: bool = False) -> None:
    (destination / "examples").mkdir(parents=True)
    ignored = shutil.ignore_patterns(".DS_Store", "__pycache__", "*.pyc")
    if not custom:
        for name in ("lantern", "input"):
            shutil.copytree(ROOT / "examples" / name, destination / "examples" / name, ignore=ignored)
    if with_network:
        shutil.copytree(ROOT / "examples" / "duet", destination / "examples" / "duet", ignore=ignored)
    shutil.copytree(ROOT / "licenses", destination / "licenses", ignore=ignored)
    (destination / "docs").mkdir()
    for name in ("api.lua", "llm-guide.md", "input.md"):
        shutil.copy2(ROOT / "docs" / name, destination / "docs" / name)
    if with_network:
        shutil.copy2(ROOT / "docs" / "networking.md", destination / "docs" / "networking.md")
    for name in ("LICENSE", "THIRD_PARTY.md"):
        shutil.copy2(ROOT / name, destination / name)


def package(binary: Path, destination: Path, *, strip: bool, make_zip: bool,
            with_network: bool = False, project: Path | None = None, symbols: tuple[Path, ...] = (),
            runtimes: tuple[tuple[Path,Path], ...] = (), launcher_exe: str | None = None) -> tuple[Path, Path | None]:
    launcher_name = game_executable_name(launcher_exe, project) if launcher_exe is not None else None
    archive = preflight(binary, destination, make_zip, with_network, project is not None)
    dependencies = audit_runtimes(binary, runtimes)
    content = content_closure(project) if project is not None else None
    sdk = None
    if len({path.name.casefold() for path in symbols}) != len(symbols):
        raise OSError("debug symbol filenames must be unique")
    for path in symbols:
        if not path.is_file():
            raise OSError(f"debug symbols are missing: {path}")
    if project is not None:
        project = project.resolve()
        if destination.resolve().is_relative_to(project):
            raise OSError("package destination must be outside the authored game")
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.mkdir()
    archive_created = False
    try:
        system = platform.system()
        if system == "Darwin":
            contents = destination / APP_NAME / "Contents"
            macos = contents / "MacOS"
            macos.mkdir(parents=True)
            resources = contents / "Resources"
            copied_binary = macos / "shiny"
            executable(macos / "launch", MAC_LAUNCH)
            executable(destination / "run-lantern.command", MAC_COMMAND)
            info = {
                "CFBundleName": "ShinyCore",
                "CFBundleDisplayName": project.name if project else "ShinyCore — Lantern",
                "CFBundleIdentifier": "dev.shinycore.game" if project else "dev.shinycore.lantern",
                "CFBundleVersion": metadata["version"].split("-")[0],
                "CFBundleShortVersionString": metadata["version"].split("-")[0],
                "CFBundlePackageType": "APPL",
                "CFBundleExecutable": "launch",
                "NSHighResolutionCapable": True,
                "NSPrincipalClass": "NSApplication",
            }
            with (contents / "Info.plist").open("wb") as file:
                plistlib.dump(info, file, sort_keys=True)
            launch_hint = "Double-click ShinyCore.app (or run-lantern.command) to play."
        else:
            resources = destination
            copied_binary = destination / ((launcher_name or "shiny.exe") if system == "Windows" else "shiny")
            if system == "Windows":
                if launcher_name:
                    launch_hint = f"Double-click {launcher_name} to play."
                else:
                    (destination / "run-lantern.bat").write_text(WINDOWS_LAUNCH, encoding="utf-8")
                    launch_hint = "Double-click run-lantern.bat to play."
            else:
                executable(destination / "run-lantern.sh", UNIX_LAUNCH)
                launch_hint = "Run ./run-lantern.sh to play."
        copy_resources(resources, with_network, project is not None)
        if project is not None:
            if content is None:
                shutil.copytree(project, resources / "game", ignore=shutil.ignore_patterns(".git", "build*", "__pycache__", ".cache", "saves", "*.pyc"))
            else:
                for item in content["files"]:
                    target = resources / "game" / item["path"]
                    target.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(project / item["path"], target)
            if system == "Darwin":
                executable(macos / "launch", MAC_LAUNCH.replace("examples/lantern", "game"))
                (destination / "run-lantern.command").rename(destination / "run-game.command")
            elif system == "Windows":
                if not launcher_name:
                    (destination / "run-lantern.bat").unlink()
                    (destination / "run-game.bat").write_text(WINDOWS_LAUNCH.replace("examples\\lantern", "game"), encoding="utf-8")
            else:
                (destination / "run-lantern.sh").unlink()
                executable(destination / "run-game.sh", UNIX_LAUNCH.replace("examples/lantern", "game"))
            if not launcher_name:
                launch_hint = "Launch the bundled game with " + ("ShinyCore.app" if system == "Darwin" else "run-game.bat" if system == "Windows" else "run-game.sh") + "."
        shutil.copy2(binary, copied_binary)
        executable(copied_binary)
        bundled_runtimes = []
        if runtimes:
            runtime_licenses = resources / "licenses" / "runtime"
            runtime_licenses.mkdir()
            for library, notice in runtimes:
                target = copied_binary.parent / library.name
                license_target = runtime_licenses / (library.name + ".txt")
                shutil.copy2(library, target)
                shutil.copy2(notice, license_target)
                bundled_runtimes.append((target, license_target))
        if symbols:
            (destination / "debug-symbols").mkdir()
            for path in symbols:
                shutil.copy2(path, destination / "debug-symbols" / path.name)
        strip_tool = shutil.which("strip") if strip and system != "Windows" else None
        if strip_tool:
            flags = ["-x"] if system == "Darwin" else ["--strip-unneeded"]
            result = subprocess.run([strip_tool, *flags, str(copied_binary)], capture_output=True, text=True)
            if result.returncode:
                detail = result.stderr.strip() or result.stdout.strip()
                raise OSError(f"strip failed: {detail}; retry with --no-strip for this executable")
        relocate_runtimes(copied_binary, dependencies)
        dependencies = audit_runtimes(copied_binary, tuple(bundled_runtimes))
        verify_relocated(dependencies)
        dependencies['relocation_verified'] = True
        try:
            result = subprocess.run([str(copied_binary), "--api"], capture_output=True, text=True, encoding="utf-8", timeout=10)
            metadata = json.loads(result.stdout) if result.returncode == 0 else None
            if not isinstance(metadata, dict) or not isinstance(metadata.get("version"), str) or not isinstance(metadata.get("modules"), dict):
                raise ValueError("missing version/modules")
        except (ValueError, subprocess.TimeoutExpired, OSError) as error:
            raise OSError("cannot read engine contract from bundled executable --api") from error
        if with_network and metadata["modules"].get("network") is not True:
            raise OSError("--with-network-examples requires a SHINY_NETWORK=ON executable")
        if project is not None:
            checked = subprocess.run([str(copied_binary), "--check-all", str(project)],
                                     capture_output=True, text=True, encoding="utf-8", timeout=60)
            if checked.returncode:
                raise OSError("game validation failed: " + checked.stderr)
            sdk = audit_sdk(resources / "game", metadata)
            checked = subprocess.run([str(copied_binary), "--check-all", str(resources / "game")],
                                     capture_output=True, text=True, encoding="utf-8", timeout=60)
            if checked.returncode:
                raise OSError("packaged game validation failed: " + checked.stderr)
        relative_binary = copied_binary.relative_to(destination).as_posix()
        if system != "Windows":
            relative_binary = "./" + relative_binary
        relative_project = (resources / "game" if project else resources / "examples" / "lantern").relative_to(destination).as_posix()
        readme = f'''ShinyCore / {project.name if project else 'Lantern'}

{launch_hint}
Built on {system} for {platform.machine()}; this package contains that platform's executable.
Move or unzip the entire folder together. The launchers resolve their own location.

Controls are defined by the bundled game. Close the window to exit.
Host debug keys require the explicit --debug-keys option.

From this package directory, validate the included game:
  "{relative_binary}" --check-all "{relative_project}"
Or run the same simulation without a window:
  "{relative_binary}" --headless "{relative_project}" --frames 180

Game source, assets, API documentation, and all dependency license notices are
beside the examples directory at {resources.relative_to(destination).as_posix()}.
The launchers work independently of the source checkout and working directory.
'''
        if system == "Windows":
            readme += "\nWindows requirement: Windows 10 version 1903 or newer (process UTF-8 paths).\n"
        if with_network:
            duet = (resources / "examples" / "duet").relative_to(destination).as_posix()
            readme += f'\nNative multiplayer demo (open twice; Z hosts, X joins):\n  "{relative_binary}" "{duet}"\nSee docs/networking.md beside the bundled examples.\n'
        (destination / "README.txt").write_text(readme, encoding="utf-8")
        write_report(destination, copied_binary, metadata, dependencies, content, sdk)
        if make_zip:
            with zipfile.ZipFile(archive, "x", zipfile.ZIP_DEFLATED, compresslevel=9) as output:
                archive_created = True
                for path in sorted(destination.rglob("*")):
                    if path.is_file():
                        output.write(path, Path(destination.name) / path.relative_to(destination))
    except BaseException:
        if archive_created:
            archive.unlink(missing_ok=True)
        shutil.rmtree(destination)
        raise
    return copied_binary, archive if make_zip else None


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", help="built native executable, such as build/shiny")
    parser.add_argument("destination", help="new package directory; it must not already exist")
    parser.add_argument("--no-strip", action="store_true", help="preserve all symbols in the copied executable")
    parser.add_argument("--no-zip", action="store_true", help="create only the runnable folder")
    parser.add_argument("--with-network-examples", action="store_true", help="include DUET and network docs; requires a network-enabled executable")
    parser.add_argument("--project", type=Path, help="validate and bundle an authored game with its own launcher")
    parser.add_argument("--launcher-exe", metavar="NAME", help="Windows: name the directly playable game executable (requires --project)")
    parser.add_argument("--symbols", type=Path, action="append", default=[], help="include a separate symbol file (repeatable)")
    parser.add_argument("--runtime", type=Path, nargs=2, action="append", default=[], metavar=("LIBRARY", "LICENSE"),
                        help="bundle an explicit native runtime and its redistribution notice (repeatable)")
    args = parser.parse_args(argv)
    binary = Path(args.binary).expanduser().resolve()
    destination = Path(os.path.abspath(os.path.expanduser(args.destination)))
    try:
        copied_binary, archive = package(binary, destination, strip=not args.no_strip,
                                        make_zip=not args.no_zip, with_network=args.with_network_examples, project=args.project,
                                        symbols=tuple(args.symbols), runtimes=tuple(tuple(pair) for pair in args.runtime),
                                        launcher_exe=args.launcher_exe)
    except (OSError, zipfile.BadZipFile) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    print(f"Created {destination}")
    print(f"Executable: {copied_binary} ({copied_binary.stat().st_size:,} bytes)")
    if archive:
        print(f"Archive: {archive} ({archive.stat().st_size:,} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
