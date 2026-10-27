#!/usr/bin/env python3
"""The harness bootstrap's smoke build: the portable game core, one test.

    python3 tools/harness_smoke.py [--platform P] [--jobs N] [--define NAME=VALUE ...]

Run from the workspace root, which is PlaySuite's checkout (projects/games.toml
in template_harness: source_dir "."), with LLVM 22.1.8, CMake and Ninja on
PATH, as the bootstrap leaves them:

1. configures the portable core (GAMES_BUILD_APPLICATION=OFF: every game core
   and engine, no GUI.Forms) with the pinned harness's
   template_harness/harness/cmake/llvm22.cmake into
   .build/native-<platform>/games-core, through ccache when it is on PATH,
   with each --define as a cache entry (for example
   FETCHCONTENT_SOURCE_DIR_QUICKJS=<a local quickjs at the pinned tag>);
2. builds storage_tests, which reads and writes saves through the real file
   system;
3. runs that one test (ctest -R ^storage$).

On macOS the toolchain file needs the LLVM 22.1.8 runtime for macOS 14 in
GAMES_LLVM_RUNTIME; when it is unset, .build/toolchain/llvm-22.1.8-macos14 is
used if present (the harness's restore_local.py prints how to build it).

The exit status is the first failing step's, or 0. Nothing outside the build
tree is written.
"""
from __future__ import annotations
import argparse
import os
from pathlib import Path
import platform as host_platform
import shutil
import subprocess
import sys

ROOT: Path = Path(__file__).resolve().parent.parent
TOOLCHAIN_FILE: Path = ROOT / 'template_harness' / 'harness' / 'cmake' / 'llvm22.cmake'
# projects/games.toml: env_prefix GAMES, build_name games.
ENV_PREFIX: str = 'GAMES'
RUNTIME_VARIABLE: str = ENV_PREFIX + '_LLVM_RUNTIME'
MACOS_RUNTIME: Path = ROOT / '.build' / 'toolchain' / 'llvm-22.1.8-macos14'
PLATFORMS: tuple[str, ...] = ('windows-x64', 'linux-x64', 'linux-arm64', 'macos-arm64')
SMOKE_TARGET: str = 'storage_tests'
SMOKE_TEST: str = '^storage$'


def host_platform_name() -> str:
    """This host as a harness platform name."""
    machine: str = host_platform.machine().lower()
    arm: bool = machine in ('arm64', 'aarch64')
    if sys.platform == 'win32':
        return 'windows-x64'
    if sys.platform == 'darwin':
        return 'macos-arm64'
    result: str = 'linux-arm64' if arm else 'linux-x64'
    return result


def smoke_environment(platform: str) -> dict[str, str] | None:
    """The build's environment, or None (after saying why) when the macOS runtime is missing."""
    environment: dict[str, str] = dict(os.environ)
    environment['HARNESS_ENV_PREFIX'] = ENV_PREFIX
    if platform == 'macos-arm64' and RUNTIME_VARIABLE not in environment:
        if not MACOS_RUNTIME.is_dir():
            print('Set ' + RUNTIME_VARIABLE + ' to the LLVM 22.1.8 runtime for macOS 14, or build it at '
                  + str(MACOS_RUNTIME) + ' (template_harness/harness/restore_local.py toolchain says how)',
                  flush=True)
            return None
        environment[RUNTIME_VARIABLE] = str(MACOS_RUNTIME)
    return environment


def configure_command(tree: Path, definitions: list[str]) -> list[str]:
    command: list[str] = [
        'cmake', '-S', str(ROOT), '-B', str(tree), '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
        '-DGAMES_BUILD_APPLICATION=OFF', '-DCMAKE_TOOLCHAIN_FILE=' + TOOLCHAIN_FILE.as_posix()]
    if shutil.which('ccache') is not None:
        command.append('-DCMAKE_C_COMPILER_LAUNCHER=ccache')
        command.append('-DCMAKE_CXX_COMPILER_LAUNCHER=ccache')
    definition: str
    for definition in definitions:
        command.append('-D' + definition)
    return command


def run_step(command: list[str], environment: dict[str, str]) -> int:
    print('+ ' + subprocess.list2cmdline(command), flush=True)
    completed: subprocess.CompletedProcess[bytes] = subprocess.run(command, cwd=ROOT, env=environment, check=False)
    return completed.returncode


def main(argv: list[str] | None = None) -> int:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--platform', choices=PLATFORMS, default='')
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--define', action='append', default=[], metavar='NAME=VALUE')
    arguments: argparse.Namespace = parser.parse_args(argv)
    if not TOOLCHAIN_FILE.is_file():
        print('No harness at ' + str(TOOLCHAIN_FILE.parent.parent.parent)
              + ': check it out first (template_harness/harness/lock.py checkout template_harness)', flush=True)
        return 1
    platform: str = arguments.platform if arguments.platform else host_platform_name()
    environment: dict[str, str] | None = smoke_environment(platform)
    if environment is None:
        return 1
    tree: Path = ROOT / '.build' / ('native-' + platform) / 'games-core'
    steps: list[list[str]] = [
        configure_command(tree, arguments.define),
        ['cmake', '--build', str(tree), '--target', SMOKE_TARGET, '--parallel', str(arguments.jobs)],
        ['ctest', '--test-dir', str(tree), '-R', SMOKE_TEST, '--output-on-failure', '--no-tests=error'],
    ]
    step: list[str]
    for step in steps:
        status: int = run_step(step, environment)
        if status != 0:
            return status
    print('Smoke build passed: ' + SMOKE_TARGET + ' on ' + platform, flush=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
