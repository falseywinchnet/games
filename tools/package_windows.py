"""Stage the PE import closure, portable archive and optional per-user installer."""
import argparse
from pathlib import Path
import re
import shutil
import subprocess
import zipfile
from package_common import ROOT, copy_resources, project_version, write_manifest

SYSTEM = {"kernel32.dll", "user32.dll", "gdi32.dll", "advapi32.dll", "comdlg32.dll",
          "ole32.dll", "oleaut32.dll", "oleacc.dll", "comctl32.dll", "shell32.dll", "shlwapi.dll",
          "usp10.dll", "windowscodecs.dll", "ws2_32.dll", "version.dll", "imm32.dll",
          "dwmapi.dll", "ntdll.dll", "msvcrt.dll", "ucrtbase.dll", "bcrypt.dll",
          "secur32.dll", "rpcrt4.dll", "setupapi.dll", "winspool.drv", "winmm.dll",
          "crypt32.dll", "normaliz.dll"}


def make_installer(bundle, output):
    installer = output / ("games-" + project_version() + "-windows-x64-setup.exe")
    script = output / "games-installer.nsi"
    lines = ['Unicode True', '!include "MUI2.nsh"', 'Name "Games"',
             'OutFile "' + str(installer) + '"', 'InstallDir "$LOCALAPPDATA\\Rainstar\\Games"',
             'RequestExecutionLevel user', '!insertmacro MUI_PAGE_WELCOME',
             '!insertmacro MUI_PAGE_DIRECTORY', '!insertmacro MUI_PAGE_INSTFILES',
             '!insertmacro MUI_PAGE_FINISH', '!insertmacro MUI_UNPAGE_CONFIRM',
             '!insertmacro MUI_UNPAGE_INSTFILES', '!insertmacro MUI_LANGUAGE "English"',
             'Section "Games"', 'SetOutPath "$INSTDIR"', 'File /r "' + str(bundle / "*") + '"',
             'CreateDirectory "$SMPROGRAMS\\Rainstar"',
             'CreateShortcut "$SMPROGRAMS\\Rainstar\\Games.lnk" "$INSTDIR\\games.exe"',
             'WriteUninstaller "$INSTDIR\\Uninstall.exe"',
             'WriteRegStr HKCU "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\RainstarGames" "DisplayName" "Games"',
             'WriteRegStr HKCU "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\RainstarGames" "UninstallString" \'$\\"$INSTDIR\\Uninstall.exe$\\"\'',
             'SectionEnd', 'Section "Uninstall"']
    directories = []
    for path in sorted(bundle.rglob("*")):
        relative = str(path.relative_to(bundle)).replace("/", "\\")
        if path.is_file():
            lines.append('Delete "$INSTDIR\\' + relative + '"')
        elif path.is_dir():
            directories.append(relative)
    # Remove only installed files and empty directories. Never recursively
    # remove a user-selected directory or any per-user save directory.
    for relative in sorted(directories, key=len, reverse=True):
        lines.append('RMDir "$INSTDIR\\' + relative + '"')
    lines += ['Delete "$INSTDIR\\Uninstall.exe"', 'RMDir "$INSTDIR"',
              'Delete "$SMPROGRAMS\\Rainstar\\Games.lnk"', 'RMDir "$SMPROGRAMS\\Rainstar"',
              'DeleteRegKey HKCU "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\RainstarGames"', 'SectionEnd']
    script.write_text("\n".join(lines) + "\n", encoding="utf-8")
    subprocess.run(["makensis", str(script)], check=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--toolkit", type=Path, required=True)
    parser.add_argument("--runtime-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=Path("dist"))
    parser.add_argument("--installer", action="store_true")
    args = parser.parse_args()
    output = args.output.resolve()
    bundle = output / "Games"
    bundle.mkdir(parents=True, exist_ok=False)
    executable = bundle / "games.exe"
    shutil.copy2(args.build / "games.exe", executable)
    candidates = {}
    for directory in (args.build, args.build / "toolkit", args.runtime_dir):
        for path in directory.glob("*.dll"):
            candidates.setdefault(path.name.lower(), path)
    pending = [executable]
    inspected = set()
    while pending:
        binary = pending.pop()
        if binary.name.lower() in inspected:
            continue
        inspected.add(binary.name.lower())
        imports = subprocess.check_output(["objdump", "-p", str(binary)], text=True)
        for name in re.findall(r"DLL Name:\s*(\S+)", imports):
            key = name.lower()
            if key in SYSTEM or key.startswith(("api-ms-win-", "ext-ms-win-")):
                continue
            if key not in candidates:
                raise RuntimeError("Missing runtime import: " + name)
            target = bundle / name
            if not target.exists():
                shutil.copy2(candidates[key], target)
            pending.append(target)
    for binary in bundle.iterdir():
        subprocess.run(["strip", "--strip-unneeded", str(binary)], check=True)
    copy_resources(args.build, args.toolkit, bundle)
    shutil.copytree(ROOT / "packaging/licenses", bundle / "licenses/runtime")
    write_manifest(bundle, "Windows", "x64")
    archive = output / ("games-" + project_version() + "-windows-x64.zip")
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as compressed:
        for path in sorted(bundle.rglob("*")):
            if path.is_file():
                compressed.write(path, "Games/" + path.relative_to(bundle).as_posix())
    if args.installer:
        make_installer(bundle, output)
    print(archive)


if __name__ == "__main__":
    main()
