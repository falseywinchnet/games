#!/usr/bin/env python3
"""Restores the compiler cache GUI.Forms published for the pinned revision.

GUI.Forms' CI publishes one archive per platform from each tested main build, as
the prerelease build-<revision>. This downloads the archive for this platform,
checks it against the digest recorded here and the revision in its manifest, and
unpacks its .ccache into the workspace. Anything unexpected leaves the cache
alone: the build then compiles the toolkit from source, which is always correct.
"""
import hashlib
import json
import subprocess
import sys
import tarfile
from pathlib import Path

from package_common import TOOLKIT_REVISION

# Our platform names to GUI.Forms' archive names.
ARCHIVES = {"windows-x64": "windows-x64", "macos-arm64": "macos-arm64", "linux-amd64": "linux-x64"}
# SHA-256 of each archive in release build-<TOOLKIT_REVISION>.
DIGESTS = {
    "linux-x64": "8fa14f5df1a998b3f3c4401d14af305d5f871444d8b13179f0e10ccaea9819ab",
    "macos-arm64": "5a2a8110c62b60d5f8defebf5009c4766253e5aaa52460eb5514455ec249044b",
    "windows-x64": "b8e5a8f440efa6b3184cfa41ab943889251b5357d961c84f2b68c601cbc7fb84",
}


def restore(platform: str) -> str:
    name = ARCHIVES.get(platform)
    if name is None:
        return "no GUI.Forms cache is published for " + platform
    archive = Path("gui-forms-cache-" + name + ".tar.gz")
    tag = "build-" + TOOLKIT_REVISION
    result = subprocess.run(["gh", "release", "download", tag, "--repo", "falseywinchnet/gui_forms",
                             "--pattern", archive.name, "--clobber"], check=False)
    if result.returncode != 0 or not archive.exists():
        return "release " + tag + " has no " + archive.name
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    expected = DIGESTS.get(name)
    if expected != digest:
        return archive.name + " digest " + digest + " is not the recorded " + str(expected)
    with tarfile.open(archive) as bundle:
        members = [m for m in bundle.getmembers()
                   if m.name == "cache-manifest.json" or m.name == ".ccache" or m.name.startswith(".ccache/")]
        for member in members:
            if member.issym() or member.islnk() or ".." in Path(member.name).parts:
                return archive.name + " holds an unsafe entry " + member.name
        bundle.extractall(".", members=members)
    manifest = json.loads(Path("cache-manifest.json").read_text(encoding="utf-8"))
    if manifest.get("revision") != TOOLKIT_REVISION:
        return "manifest revision " + str(manifest.get("revision")) + " is not the pin"
    return "restored " + archive.name + " (" + str(manifest.get("compiler")) + ")"


def main() -> None:
    print("GUI.Forms compiler cache: " + restore(sys.argv[1]))


if __name__ == "__main__":
    main()
