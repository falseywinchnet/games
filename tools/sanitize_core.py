#!/usr/bin/env python3
"""The sanitized portable game core: every core test under ASan and UBSan (and LSan).

    python3 tools/sanitize_core.py [--platform P] [--jobs N] [--define NAME=VALUE ...]
                                   [--tests REGEX]

Run from the workspace root (PlaySuite's checkout) with the pinned harness at
template_harness/ and LLVM 22.1.8, CMake and Ninja on PATH, as for
tools/harness_smoke.py, whose environment and toolchain this shares:

1. configures the portable core (GAMES_BUILD_APPLICATION=OFF: every game core
   and engine, QuickJS, no GUI.Forms) into .build/native-<platform>/games-core-sanitize
   with the harness's toolchain file and the harness's sanitizer options
   (ci_build_steps.sanitizer_options: AddressSanitizer and UndefinedBehaviorSanitizer
   everywhere, LeakSanitizer on Linux and macOS, every report fatal), through
   ccache when it is on PATH, with each --define as a cache entry;
2. builds everything;
3. runs every test except the `design` label (deterministic game mechanics, which
   CI never runs), or those --tests selects, with the sanitizer environment.

Leak suppressions are tools/sanitizer/lsan.supp; each entry names its reason.
GAMES_SANITIZER_DEBUG_INFO=full builds with -g instead of line tables.

The exit status is the first failing step's, or 0. Nothing outside the build tree is
written.
"""
from __future__ import annotations
import argparse
import os
from pathlib import Path
import sys

import harness_smoke

ROOT: Path = harness_smoke.ROOT
HARNESS_TOOLS: Path = ROOT / 'template_harness' / 'harness'
PROJECT_FILE: Path = ROOT / 'template_harness' / 'projects' / 'games.toml'
LSAN_SUPPRESSIONS: Path = ROOT / 'tools' / 'sanitizer' / 'lsan.supp'
# How many times the shell's own test timeouts (CMakeLists.txt) the sanitized tests get.
TIMEOUT_SCALE: int = 4
HOSTS: dict[str, str] = {'windows-x64': 'windows', 'macos-arm64': 'macos',
                         'linux-x64': 'linux', 'linux-arm64': 'linux'}


def main(argv: list[str] | None = None) -> int:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--platform', choices=harness_smoke.PLATFORMS, default='')
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--define', action='append', default=[], metavar='NAME=VALUE')
    parser.add_argument('--tests', default='', metavar='REGEX', help='ctest -R selection; default every test')
    arguments: argparse.Namespace = parser.parse_args(argv)
    if not harness_smoke.TOOLCHAIN_FILE.is_file():
        print('No harness at ' + str(HARNESS_TOOLS.parent)
              + ': check it out first (template_harness/harness/lock.py checkout template_harness)', flush=True)
        return 1
    # ci_build_steps reads the project file when it is imported (the debug-info variable's
    # name), so the project must be named first; CI's harness-checkout step already has.
    os.environ.setdefault('HARNESS_PROJECT', str(PROJECT_FILE))
    sys.path.insert(0, str(HARNESS_TOOLS))
    import ci_build_steps

    platform: str = arguments.platform if arguments.platform else harness_smoke.host_platform_name()
    environment: dict[str, str] | None = harness_smoke.smoke_environment(platform)
    if environment is None:
        return 1
    debug_info: str = ci_build_steps.debug_info_mode('', dict(os.environ))
    options: list[str]
    sanitizer_environment: dict[str, str]
    options, sanitizer_environment = ci_build_steps.sanitizer_options(HOSTS[platform], LSAN_SUPPRESSIONS,
                                                                      debug_info)
    environment.update(sanitizer_environment)
    tree: Path = ROOT / '.build' / ('native-' + platform) / 'games-core-sanitize'
    configure: list[str] = harness_smoke.configure_command(tree, arguments.define)
    # The harness's options set the build type (RelWithDebInfo) after the smoke's Release.
    # Instrumented tests run several times slower than the Release timeouts allow for.
    configure.extend(options)
    configure.append('-DGAMES_TEST_TIMEOUT_SCALE=' + str(TIMEOUT_SCALE))
    test: list[str] = ['ctest', '--test-dir', str(tree), '-LE', 'design', '--output-on-failure',
                       '--no-tests=error', '--timeout', '900', '--parallel', str(arguments.jobs)]
    if arguments.tests:
        test.extend(['-R', arguments.tests])
    steps: list[list[str]] = [
        configure,
        ['cmake', '--build', str(tree), '--parallel', str(arguments.jobs)],
        test,
    ]
    step: list[str]
    for step in steps:
        status: int = harness_smoke.run_step(step, environment)
        if status != 0:
            return status
    print('Sanitized core passed on ' + platform, flush=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
