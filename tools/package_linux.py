"""Package the native Linux application for Ubuntu 24.04 or compatible systems."""
import argparse
from pathlib import Path
import platform
import shutil
import subprocess
import tarfile
from package_common import ROOT, copy_resources, project_version, write_manifest


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--toolkit", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=Path("dist"))
    args = parser.parse_args()
    output = args.output.resolve()
    bundle = output / "Games"
    bundle.mkdir(parents=True, exist_ok=False)
    shutil.copy2(args.build / "games", bundle / "games")
    libraries = list((args.build / "toolkit").glob("libgui_forms_application.so*"))
    if not libraries:
        raise RuntimeError("Missing GUI.Forms application library")
    for source in libraries:
        shutil.copy2(source, bundle / source.name)
    for binary in [bundle / "games"] + list(bundle.glob("*.so*")):
        subprocess.run(["patchelf", "--set-rpath", "$ORIGIN", str(binary)], check=True)
        subprocess.run(["strip", "--strip-unneeded", str(binary)], check=True)
        dependencies = subprocess.check_output(["ldd", str(binary)], text=True)
        if "not found" in dependencies:
            raise RuntimeError(dependencies)
    copy_resources(args.build, args.toolkit, bundle)
    arch = "arm64" if platform.machine() == "aarch64" else "amd64"
    write_manifest(bundle, "Linux", arch)
    stem = "games-" + project_version() + "-linux-" + arch
    with tarfile.open(output / (stem + ".tar.gz"), "w:gz") as archive:
        archive.add(bundle, arcname="Games")
    root = output / "deb-root"
    installed = root / "usr/lib/rainstar-games"
    shutil.copytree(bundle, installed)
    launcher = root / "usr/bin/rainstar-games"
    launcher.parent.mkdir(parents=True)
    launcher.write_text('#!/bin/sh\nexec /usr/lib/rainstar-games/games "$@"\n', encoding="utf-8")
    launcher.chmod(0o755)
    desktop = root / "usr/share/applications/rainstar-games.desktop"
    desktop.parent.mkdir(parents=True)
    desktop.write_text("[Desktop Entry]\nType=Application\nName=Games\nExec=rainstar-games\n"
                       "Icon=rainstar-games\nCategories=Game;\nTerminal=false\n", encoding="utf-8")
    icon = root / "usr/share/icons/hicolor/256x256/apps/rainstar-games.png"
    icon.parent.mkdir(parents=True)
    shutil.copy2(ROOT / "assets/Games.png", icon)
    control = root / "DEBIAN/control"
    control.parent.mkdir()
    # dpkg-shlibdeps uses a Debian source metadata directory even for -O.
    metadata = output / "debian/control"
    metadata.parent.mkdir()
    metadata.write_text("Source: rainstar-games\nSection: games\nPriority: optional\n"
                        "Maintainer: Astra <noreply@rainstar.invalid>\n\n"
                        "Package: rainstar-games\nArchitecture: any\nDescription: Native game collection\n", encoding="utf-8")
    dependencies = subprocess.check_output(
        ["dpkg-shlibdeps", "-O", "-l" + str(installed), "-e" + str(installed / "games"),
         "--ignore-missing-info"], cwd=output, text=True).strip().removeprefix("shlibs:Depends=")
    control.write_text("Package: rainstar-games\nVersion: " + project_version() + "\nArchitecture: " + arch +
        "\nMaintainer: Astra <noreply@rainstar.invalid>\nSection: games\nPriority: optional\nDepends: " +
        dependencies + ", libasound2t64\nDescription: Native card games, puzzles and arcade scenes\n", encoding="utf-8")
    subprocess.run(["dpkg-deb", "--root-owner-group", "--build", str(root), str(output / (stem + ".deb"))], check=True)


if __name__ == "__main__":
    main()
