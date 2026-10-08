#!/usr/bin/env python3
"""Restores what GUI.Forms published for the pinned revision.

GUI.Forms' CI publishes one archive per platform from each tested main build, as
the prerelease build-<revision>. This downloads the archive for this platform,
checks it against the digest recorded here and the revision in its manifest, and
unpacks its compiler cache (.ccache) and, on macOS, the LLVM 22 runtime built for
macOS 14 (.build/toolchain). A missing or mismatched cache only means compiling
from source. The macOS runtime is then verified against this machine's compiler
and SDK, and rebuilt from GUI.Forms' pinned LLVM source when it does not match.
"""
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tarfile
from pathlib import Path

from package_common import TOOLKIT_REVISION

# Our platform names to GUI.Forms' archive names.
ARCHIVES = {"windows-x64": "windows-x64", "macos-arm64": "macos-arm64",
            "linux-amd64": "linux-x64", "linux-arm64": "linux-arm64"}
# SHA-256 of each archive in release build-<TOOLKIT_REVISION>.
DIGESTS = {
    "linux-arm64": "5a7dabec0c69a517c3d904f77d1b525ff6c5fd59cbc8150ec4f1d05b1758559f",
    "linux-x64": "03a03c2fc04c60b62d21ba919907e06c9d3513fc0f345569cab713138a0c8103",
    "macos-arm64": "7f735d7133786e5f8c4c0d29da596fc579ba84505c558ce92529207690eaf3c8",
    "windows-x64": "c18ccd5020ffe133cf6386e5a26d21d32ae25cec53f70a88faae17ae7e227d8e",
}
RUNTIME = Path(".build/toolchain/llvm-22.1.8-macos14")


def wanted(name: str) -> bool:
    for root in (".ccache", ".build/toolchain"):
        if name == root or name.startswith(root + "/"):
            return True
    return name == "cache-manifest.json"


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
    if DIGESTS.get(name) != digest:
        return archive.name + " digest " + digest + " is not the recorded " + str(DIGESTS.get(name))
    with tarfile.open(archive) as bundle:
        members = []
        for member in bundle.getmembers():
            if not wanted(member.name):
                continue
            if member.islnk() or ".." in Path(member.name).parts or Path(member.name).is_absolute():
                return archive.name + " holds an unsafe entry " + member.name
            # The runtime's dylib version links stay inside lib/.
            if member.issym() and ("/" in member.linkname or member.linkname.startswith(".")):
                return archive.name + " holds an unsafe link " + member.name
            members.append(member)
        bundle.extractall(".", members=members)
    archive.unlink()
    manifest = json.loads(Path("cache-manifest.json").read_text(encoding="utf-8"))
    if manifest.get("revision") != TOOLKIT_REVISION:
        return "manifest revision " + str(manifest.get("revision")) + " is not the pin"
    return "restored " + name + " for " + TOOLKIT_REVISION[:7]


def prepare_runtime() -> None:
    tools = Path("gui_forms/tools")
    check = subprocess.run([sys.executable, str(tools / "macos_runtime_manifest.py"), str(RUNTIME)], check=False)
    if check.returncode != 0:
        print("Building the LLVM 22 runtime for macOS 14 from GUI.Forms' pinned source", flush=True)
        shutil.rmtree(RUNTIME, ignore_errors=True)
        work = Path(os.environ.get("RUNNER_TEMP", ".build")) / "llvm-runtimes"
        subprocess.run([sys.executable, str(tools / "build_macos_runtimes.py"), "--work", str(work),
                        "--prefix", str(RUNTIME.resolve()), "--jobs", "4"], check=True)
        subprocess.run([sys.executable, str(tools / "macos_runtime_manifest.py"), str(RUNTIME)], check=True)
    subprocess.run([sys.executable, str(tools / "audit_macos_minimum.py"), str(RUNTIME / "lib")], check=True)


def main() -> None:
    platform = sys.argv[1]
    print("GUI.Forms cache: " + restore(platform), flush=True)
    if platform == "macos-arm64":
        prepare_runtime()


if __name__ == "__main__":
    main()
