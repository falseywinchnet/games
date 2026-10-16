#!/usr/bin/env python3
"""Publishes a built library file and pins it, so Git keeps a few hundred bytes for it.

The library (assets/books/library.plb) is rebuilt whole for any change to any book, and
is tens of megabytes: kept in Git, every rebuild would stay in the history for good. It
is published instead as a download of this repository's `library-data` release, named
by its content (library-<first 16 hex digits of its SHA-256>.plb, so a name never
changes meaning), and pinned in ../pinned_assets.json. Every build fetches and checks it
(tools/pinned_assets.py, called by tools/prepare_portable_assets.py) and ships it inside
the suite's packages.

    publish_library.py [library.plb]

Needs the GitHub CLI signed in with permission to edit releases. The release is created
the first time, never marked latest, and must not be removed with old app releases.
"""

import hashlib
import json
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
GAME = HERE.parent
RELEASE = "library-data"
PIN_PATH = "books/library.plb"


def gh(*arguments: str, check: bool = True) -> subprocess.CompletedProcess:
    return subprocess.run(["gh", *arguments], cwd=GAME, capture_output=True, text=True, check=check)


def main() -> int:
    library = Path(sys.argv[1]) if len(sys.argv) > 1 else GAME / "assets" / PIN_PATH
    data = library.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    name = "library-%s.plb" % digest[:16]
    repository = gh("repo", "view", "--json", "nameWithOwner", "-q", ".nameWithOwner").stdout.strip()
    if gh("release", "view", RELEASE, check=False).returncode != 0:
        gh("release", "create", RELEASE, "--title", "Pocket Library data", "--latest=false",
           "--notes", "The Pocket Library's books, one file per build of the library, named by its "
           "content. Builds fetch the one games/pocketlibrary/pinned_assets.json names and ship it "
           "inside PlaySuite. Not an app release: keep it when old releases are removed.")
    assets = json.loads(gh("release", "view", RELEASE, "--json", "assets").stdout)["assets"]
    if not any(asset["name"] == name for asset in assets):
        upload = HERE / name
        upload.write_bytes(data)
        try:
            gh("release", "upload", RELEASE, str(upload))
        finally:
            upload.unlink()
    url = "https://github.com/%s/releases/download/%s/%s" % (repository, RELEASE, name)
    pin = {"files": [{"path": PIN_PATH, "url": url, "size": len(data), "sha256": digest}]}
    manifest = GAME / "pinned_assets.json"
    manifest.write_text(json.dumps(pin, indent=1) + "\n", encoding="utf-8")
    print("pinned %s (%d bytes) at %s" % (name, len(data), url))
    return 0


if __name__ == "__main__":
    sys.exit(main())
