#!/usr/bin/env python3
"""Fails if firmware, other proprietary data or the original names were ever committed.

Checks every file in every commit reachable from REV (default HEAD), so anything committed and deleted again is
caught too (it would stay in the history). A file is rejected when:
  - its extension is a data or firmware type: .bin .img .cab .exe .dmg .flash .nd2p .nd2_bank .syx .mid .wav .mp3
    .aif .aiff (any letter case), or
  - it is an image or font anywhere but the built-in default skin (skins/default/), or
  - it is larger than 1 MB (12 MB in skins/default/, whose art is drawn at twice the size), or
  - its MD5 equals a known OS update or factory file (renamed, any size), or
  - it is a text file that contains one of the original names.
Commit messages are checked for the names as well. The names are stored encoded here, so this file does not
contain them either.

Usage: scripts/check_no_data.py [REV]
Needs the full history (CI: actions/checkout with fetch-depth: 0).
"""
import base64
import hashlib
import subprocess
import sys

FORBIDDEN_EXTENSIONS = (".bin", ".img", ".cab", ".exe", ".dmg", ".flash", ".nd2p", ".nd2_bank", ".syx", ".mid",
                        ".wav", ".mp3", ".aif", ".aiff")
IMAGE_FONT_EXTENSIONS = (".png", ".jpg", ".jpeg", ".gif", ".tga", ".bmp", ".webp", ".psd", ".xcf", ".ttf", ".otf")
DEFAULT_SKIN = "skins/default/"
MAX_BYTES = 1024 * 1024
MAX_SKIN_BYTES = 12 * 1024 * 1024
KNOWN_MD5 = {"c34cda0b23512760da2e86847bf9ee10": "the OS image (os.cab)",
             "7bb23a7fe3e73d58bfdad461884b258c": "the OS updater (Windows)",
             "088eb8c861937b7337d358e1d5ffa851": "the OS update archive",
             "55b8cff72bb88e2ccaf7830e8ed796ea": "the OS update disk image",
             "5a3672dc50896f2ba233073771fdb6f9": "the factory bank archive",
             "cb36307a9143b8255fba8fc762fadab7": "the factory memory"}
NAMES = [base64.b64decode(n).decode() for n in ("bm9yZA==", "Y2xhdmlh")]   # lower case


def git(*args):
    return subprocess.run(["git", *args], check=True, capture_output=True).stdout


def has_name(data):
    low = data.lower()
    return [n for n in NAMES if n.encode() in low]


def main():
    rev = sys.argv[1] if len(sys.argv) > 1 else "HEAD"
    commits = git("rev-list", rev).decode().split()
    checked = {}                     # blob -> reasons
    problems = []
    for commit in commits:
        message = git("log", "-1", "--format=%B", commit)
        if has_name(message):
            problems.append("%s  commit message: contains an original name" % commit[:10])
        # Files added or changed by this commit (against its first parent; the root commit lists everything).
        out = git("diff-tree", "-r", "-z", "--root", "--no-commit-id", "--no-renames", "--diff-filter=AMT", commit)
        fields = out.split(b"\0")
        i = 0
        while i + 1 < len(fields) and fields[i]:
            meta, path = fields[i].decode(), fields[i + 1].decode("utf-8", "replace")
            i += 2
            new_mode, new_blob = meta.split()[1], meta.split()[3]
            if new_mode == "160000":         # submodule link, no file content
                continue
            lower = path.lower()
            in_skin = path.startswith(DEFAULT_SKIN)
            reasons = []
            if lower.endswith(FORBIDDEN_EXTENSIONS):
                reasons.append("data file type " + path[path.rfind("."):])
            if lower.endswith(IMAGE_FONT_EXTENSIONS) and not in_skin:
                reasons.append("image or font outside " + DEFAULT_SKIN)
            if has_name(path.encode()):
                reasons.append("file name contains an original name")
            key = (new_blob, in_skin)
            if key not in checked:
                size = int(git("cat-file", "-s", new_blob))
                data = git("cat-file", "blob", new_blob)
                why = []
                limit = MAX_SKIN_BYTES if in_skin else MAX_BYTES
                if size > limit:
                    why.append("%d bytes (over %d MB)" % (size, limit // (1024 * 1024)))
                md5 = hashlib.md5(data).hexdigest()
                if md5 in KNOWN_MD5:
                    why.append("content is " + KNOWN_MD5[md5])
                if b"\0" not in data[:8192] and has_name(data):
                    why.append("text contains an original name")
                checked[key] = why
            reasons += checked[key]
            if reasons:
                problems.append("%s  %s: %s" % (commit[:10], path, "; ".join(reasons)))
    if problems:
        print("Problems found in the history (firmware, data and the original names must never be committed):")
        print("\n".join(problems))
        print("\nRemoving the file in a new commit is not enough: the commit that added it must be removed from the")
        print("branch before it is merged. Ask for help with this before merging.")
        return 1
    print("check_no_data: %d commits, %d files checked: no data files, no original names" % (len(commits), len(checked)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
