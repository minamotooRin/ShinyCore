#!/usr/bin/env python3
"""Check relative Markdown links in the tracked documentation tree."""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
LINK = re.compile(r"\]\(([^)]+)\)")
DIRECTORIES = ("docs", "examples", "src", "include", "lua", "tools", "tests", "cmake", "benchmarks", "licenses")


def main() -> int:
    files = [ROOT / "README.md", ROOT / "AGENTS.md", ROOT / "THIRD_PARTY.md"]
    files += [path for directory in DIRECTORIES for path in (ROOT / directory).rglob("*.md")]
    missing = []
    for file in sorted(files):
        if not file.is_file():
            continue
        for line_number, line in enumerate(file.read_text(encoding="utf-8").splitlines(), 1):
            for match in LINK.finditer(line):
                target = match.group(1).split("#", 1)[0]
                if not target or target.startswith(("http:", "https:", "mailto:", "/")):
                    continue
                if target.startswith("<") and target.endswith(">"):
                    target = target[1:-1]
                if " " in target or not (file.parent / target).exists():
                    missing.append(f"{file.relative_to(ROOT)}:{line_number}: {target}")
    if missing:
        print("\n".join(missing))
        return 1
    print(f"Checked {len(files)} Markdown files: all relative links resolve.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
