"""Stage the PE import closure, portable archive and optional per-user installer."""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import zipfile
from package_common import (LEGACY_WINDOWS_INSTALL, VENDOR, WINDOWS_UNINSTALL_KEY, ROOT, copy_resources, project_version,
                            write_manifest)

# Windows' own DLLs, never shipped: GUI.Forms' list (tools/package_programs.py
# WINDOWS_SYSTEM_DLLS at the pin). UIAutomationCore is its UI Automation provider.
SYSTEM = {"kernel32.dll", "user32.dll", "gdi32.dll", "advapi32.dll", "comdlg32.dll",
          "ole32.dll", "oleaut32.dll", "oleacc.dll", "comctl32.dll", "shell32.dll", "shlwapi.dll",
          "usp10.dll", "windowscodecs.dll", "ws2_32.dll", "version.dll", "imm32.dll",
          "dwmapi.dll", "ntdll.dll", "msvcrt.dll", "ucrtbase.dll", "bcrypt.dll",
          "secur32.dll", "rpcrt4.dll", "setupapi.dll", "winspool.drv", "winmm.dll",
          "crypt32.dll", "normaliz.dll", "uxtheme.dll", "msimg32.dll", "shcore.dll",
          "dbghelp.dll", "psapi.dll", "userenv.dll", "iphlpapi.dll", "uiautomationcore.dll"}


def find_makensis():
    """The official NSIS when the machine has it (GitHub's Windows image ships 3.10),
    otherwise the one on PATH. MSYS2's NSIS 3.13 pairs 32-bit installer stubs with
    64-bit plugins, so makensis rejects every plugin and MUI's pages cannot build."""
    for root in (os.environ.get("NSIS_HOME", ""), os.environ.get("ProgramFiles(x86)", ""), os.environ.get("ProgramFiles", ""),
                 "C:/Program Files (x86)", "C:/Program Files"):
        if not root:
            continue
        for candidate in (Path(root) / "makensis.exe", Path(root) / "NSIS" / "makensis.exe"):
            if candidate.is_file():
                return str(candidate)
    return "makensis"


def make_installer(bundle, output):
    version = project_version()
    installer = output / ("playsuite-" + version + "-windows-x64-setup.exe")
    script = output / "games-installer.nsi"
    key = '"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\' + WINDOWS_UNINSTALL_KEY + '"'
    menu = "$SMPROGRAMS\\" + VENDOR
    # New installs go under the vendor. An upgrade stays where it is: the recorded
    # InstallLocation, else 0.5.x's folder when it holds the application. /D= still wins.
    default = "$LOCALAPPDATA\\" + VENDOR + "\\PlaySuite"
    lines = ['Unicode True', '!include "MUI2.nsh"', 'Name "PlaySuite"',
             'OutFile "' + str(installer).replace("/", "\\") + '"', 'InstallDir "' + default + '"',
             'InstallDirRegKey HKCU ' + key + ' "InstallLocation"',
             'VIProductVersion "' + version + '.0"', 'VIAddVersionKey "ProductName" "PlaySuite"',
             'VIAddVersionKey "CompanyName" "' + VENDOR + '"', 'VIAddVersionKey "FileDescription" "PlaySuite installer"',
             'VIAddVersionKey "FileVersion" "' + version + '"', 'VIAddVersionKey "ProductVersion" "' + version + '"',
             'RequestExecutionLevel user', '!insertmacro MUI_PAGE_WELCOME',
             '!insertmacro MUI_PAGE_DIRECTORY', '!insertmacro MUI_PAGE_INSTFILES',
             '!insertmacro MUI_PAGE_FINISH', '!insertmacro MUI_UNPAGE_CONFIRM',
             '!insertmacro MUI_UNPAGE_INSTFILES', '!insertmacro MUI_LANGUAGE "English"',
             'Function .onInit', 'StrCmp $INSTDIR "' + default + '" 0 keep',
             'IfFileExists "' + LEGACY_WINDOWS_INSTALL + '\\games.exe" 0 keep',
             'StrCpy $INSTDIR "' + LEGACY_WINDOWS_INSTALL + '"', 'keep:', 'FunctionEnd',
             'Section "PlaySuite"', 'SetOutPath "$INSTDIR"', 'File /r "' + str(bundle / "*").replace("/", "\\") + '"',
             # 0.5.x's shortcuts lived under Rainstar.
             'Delete "$SMPROGRAMS\\Rainstar\\Games.lnk"', 'Delete "$SMPROGRAMS\\Rainstar\\PlaySuite.lnk"',
             'RMDir "$SMPROGRAMS\\Rainstar"',
             'CreateDirectory "' + menu + '"',
             'CreateShortcut "' + menu + '\\PlaySuite.lnk" "$INSTDIR\\games.exe"',
             'WriteUninstaller "$INSTDIR\\Uninstall.exe"',
             'WriteRegStr HKCU ' + key + ' "DisplayName" "PlaySuite"',
             'WriteRegStr HKCU ' + key + ' "Publisher" "' + VENDOR + '"',
             'WriteRegStr HKCU ' + key + ' "DisplayVersion" "' + version + '"',
             'WriteRegStr HKCU ' + key + ' "DisplayIcon" \'$\\"$INSTDIR\\games.exe$\\"\'',
             'WriteRegStr HKCU ' + key + ' "InstallLocation" "$INSTDIR"',
             'WriteRegStr HKCU ' + key + ' "UninstallString" \'$\\"$INSTDIR\\Uninstall.exe$\\"\'',
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
    # The vendor folder of a new install goes too when nothing else is left in it.
    lines += ['Delete "$INSTDIR\\Uninstall.exe"', 'RMDir "$INSTDIR"',
              'RMDir "$LOCALAPPDATA\\' + VENDOR + '"',
              'Delete "' + menu + '\\PlaySuite.lnk"', 'RMDir "' + menu + '"',
              'Delete "$SMPROGRAMS\\Rainstar\\Games.lnk"',
              'Delete "$SMPROGRAMS\\Rainstar\\PlaySuite.lnk"', 'RMDir "$SMPROGRAMS\\Rainstar"',
              'DeleteRegKey HKCU ' + key, 'SectionEnd']
    script.write_text("\n".join(lines) + "\n", encoding="utf-8")
    makensis = find_makensis()
    print("Building the installer with", makensis, flush=True)
    subprocess.run([makensis, str(script)], check=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--toolkit", type=Path, required=True)
    parser.add_argument("--runtime-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=Path("dist"))
    parser.add_argument("--installer", action="store_true")
    args = parser.parse_args()
    output = args.output.resolve()
    bundle = output / "PlaySuite"
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
    runtime_notices = bundle / "licenses/runtime"
    runtime_notices.mkdir()
    bundled = {path.name.lower() for path in bundle.iterdir()}
    for notice in (ROOT / "packaging/licenses").glob("*.txt"):
        # GCC's runtime notices only when its runtime is shipped (a MINGW64 build)
        if notice.name.startswith("GCC-") and not bundled & {"libstdc++-6.dll", "libgcc_s_seh-1.dll"}:
            continue
        shutil.copy2(notice, runtime_notices / notice.name)
    # LLVM's libc++ and libunwind (a CLANG64 build)
    if bundled & {"libc++.dll", "libunwind.dll"}:
        shutil.copytree(ROOT / "packaging/licenses/llvm", runtime_notices / "LLVM")
    write_manifest(bundle, "Windows", "x64")
    archive = output / ("playsuite-" + project_version() + "-windows-x64.zip")
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as compressed:
        for path in sorted(bundle.rglob("*")):
            if path.is_file():
                compressed.write(path, "PlaySuite/" + path.relative_to(bundle).as_posix())
    if args.installer:
        make_installer(bundle, output)
    print(archive)


if __name__ == "__main__":
    main()
