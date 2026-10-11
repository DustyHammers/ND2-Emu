#!/usr/bin/env python3
"""Release notes from the README's "What's new" section.

  release_notes.py            prints the notes of the version in CMakeLists.txt
  release_notes.py --check    fails if the README has no notes for that version (CI, every pull request)

Each version is a "### X.Y.Z" heading under "## What's new", followed by its bullet points.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent


def project_version():
    m = re.search(r"^project\(ND2Emu VERSION ([0-9.]+)", (ROOT / "CMakeLists.txt").read_text(), re.M)
    if not m:
        sys.exit("CMakeLists.txt: no project version")
    return m.group(1)


def notes(version):
    lines = (ROOT / "README.md").read_text().splitlines()
    try:
        start = lines.index("## What's new")
    except ValueError:
        return None
    out, inside = [], False
    for line in lines[start + 1:]:
        if line.startswith("## "):
            break
        if line.startswith("### "):
            if inside:
                break
            inside = line[4:].strip() == version
            continue
        if inside:
            out.append(line)
    text = "\n".join(out).strip()
    return text or None


def main():
    version = project_version()
    text = notes(version)
    if text is None:
        sys.exit(f"README.md: no notes for version {version} (add \"### {version}\" under \"## What's new\")")
    if "--check" in sys.argv[1:]:
        print(f"README.md has notes for version {version}")
    else:
        print(text)


if __name__ == "__main__":
    main()
