"""Bundle native Mach-O dependencies and produce an ad-hoc-signed app and installer."""
import argparse
import hashlib
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
from package_common import ROOT, copy_resources, project_version, write_manifest


def run(arguments):
    return subprocess.check_output(arguments, text=True).strip()


def rpaths(path):
    return re.findall(r"cmd LC_RPATH\n\s+cmdsize \d+\n\s+path (.*?) \(offset", run(["otool", "-l", str(path)]))


def dependencies(path):
    return [line.strip().split(" (", 1)[0] for line in run(["otool", "-L", str(path)]).splitlines()[1:]]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--toolkit", type=Path, required=True)
    parser.add_argument("--llvm", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=Path("dist"))
    args = parser.parse_args()
    build = args.build.resolve()
    source_app = build / "games.app"
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    app = output / "Games.app"
    shutil.copytree(source_app, app)
    executable = app / "Contents/MacOS/games"
    original_executable = source_app / "Contents/MacOS/games"
    frameworks = app / "Contents/Frameworks"
    frameworks.mkdir(exist_ok=True)
    resources = app / "Contents/Resources"
    copy_resources(build, args.toolkit, resources)
    compiler = run([str(args.llvm / "bin/clang++"), "--version"])
    if "20.1.8" not in compiler:
        raise RuntimeError("Review the runtime license pin before packaging another LLVM version")
    shutil.copytree(ROOT / "packaging/licenses/llvm", resources / "licenses/LLVM")
    plist_path = app / "Contents/Info.plist"
    plist = plistlib.loads(plist_path.read_bytes())
    plist["LSMinimumSystemVersion"] = "15.0"
    plist_path.write_bytes(plistlib.dumps(plist))

    def expand(value, owner):
        return Path(value.replace("@loader_path", str(owner.parent)).replace("@executable_path", str(original_executable.parent)))

    def resolve(value, owner):
        if value.startswith("@rpath/"):
            relative = value[len("@rpath/"):]
            candidates = [expand(base, owner) / relative for base in rpaths(owner)]
            candidates += [expand(base, original_executable) / relative for base in rpaths(original_executable)]
        else:
            candidates = [expand(value, owner)]
        for candidate in candidates:
            if candidate.is_absolute() and candidate.is_file():
                return candidate.resolve()
        raise RuntimeError("Unresolved dependency " + value + " in " + str(owner))

    pending = [(original_executable, executable)]
    seen = set()
    origins = {}
    while pending:
        original, target = pending.pop(0)
        if target in seen:
            continue
        seen.add(target)
        identifiers = run(["otool", "-D", str(original)]).splitlines()[1:]
        own_id = identifiers[0] if identifiers else None
        for dependency in dependencies(original):
            if dependency == own_id or dependency.startswith(("/System/", "/usr/lib/")):
                continue
            source = resolve(dependency, original)
            name = Path(dependency).name
            destination = frameworks / name
            digest = hashlib.sha256(source.read_bytes()).hexdigest()
            if name in origins and origins[name] != digest:
                raise RuntimeError("Dependency basename collision: " + name)
            if name not in origins:
                shutil.copy2(source, destination)
                destination.chmod(0o755)
                run(["install_name_tool", "-id", "@rpath/" + name, str(destination)])
                origins[name] = digest
                pending.append((source, destination))
            prefix = "@executable_path/../Frameworks/" if target == executable else "@loader_path/"
            run(["install_name_tool", "-change", dependency, prefix + name, str(target)])
        for path in rpaths(target):
            if not path.startswith("@"):
                run(["install_name_tool", "-delete_rpath", path, str(target)])
    # CMake also copies the real versioned filename; remove only redundant
    # copies that are not members of the resolved dependency closure.
    for library in frameworks.iterdir():
        if library not in seen:
            library.unlink()
    for target in seen:
        identifiers = run(["otool", "-D", str(target)]).splitlines()[1:]
        own_id = identifiers[0] if identifiers else None
        for dependency in dependencies(target):
            if dependency == own_id or dependency.startswith(("/System/", "/usr/lib/")):
                continue
            if not dependency.startswith(("@loader_path/", "@executable_path/../Frameworks/")):
                raise RuntimeError("Nonportable dependency: " + dependency)
            if not (frameworks / Path(dependency).name).is_file():
                raise RuntimeError("Missing bundled dependency: " + dependency)
        run(["strip", "-S", "-x", str(target)])
        run(["codesign", "--force", "--sign", "-", str(target)])
    write_manifest(resources, "macOS", "arm64")
    run(["codesign", "--force", "--deep", "--sign", "-", str(app)])
    run(["codesign", "--verify", "--deep", "--strict", str(app)])
    stem = "games-" + project_version() + "-macos-arm64"
    run(["ditto", "-c", "-k", "--keepParent", str(app), str(output / (stem + ".zip"))])
    installer_root = output / "installer-root"
    shutil.copytree(app, installer_root / "Games.app")
    components = output / "components.plist"
    run(["pkgbuild", "--analyze", "--root", str(installer_root), str(components)])
    records = plistlib.loads(components.read_bytes())
    for record in records:
        record["BundleIsRelocatable"] = False
    components.write_bytes(plistlib.dumps(records))
    run(["pkgbuild", "--root", str(installer_root), "--component-plist", str(components),
         "--install-location", "/Applications",
         "--identifier", "org.rainstar.games", "--version", project_version(), str(output / (stem + ".pkg"))])


if __name__ == "__main__":
    main()
