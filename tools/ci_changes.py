#!/usr/bin/env python3
"""Which optional CI suites a change needs: the portable text and audio consumer checks.

    python3 tools/ci_changes.py [--base SHA --head SHA]

Every check of a pull request now runs in one workflow (applications.yml) so that one
always-reporting job, "PlaySuite CI summary", can stand for all of them. A suite that
only some changes need is therefore chosen here instead of by a workflow path filter,
which would leave its check unreported on other pull requests.

Writes text=true|false and audio=true|false to GITHUB_OUTPUT (or prints them). Without
a base, or with a base git cannot compare (a new branch's all-zero "before", history
the checkout lacks), every suite runs. Patterns follow GitHub's path filters: `*`
stays inside one folder, `**` crosses folders.
"""
from __future__ import annotations
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT: Path = Path(__file__).resolve().parent.parent
# GUI.Forms text masks as an outside consumer (native-text.yml).
TEXT: tuple[str, ...] = (
    'src/text_requests.*', 'src/game_text.*', 'src/bar_music.*', 'assets/fonts/**', 'games/eggy/**',
    'games/fourpegs/**', 'games/atomprobe/**', 'games/switchbox/**', 'tests/*frame*', 'tests/*text*',
    'tests/portable_text/**', 'tools/restore_*.py', 'tools/package_common.py', 'tools/ci_changes.py',
    'harness.lock', '.github/actions/**', '.github/workflows/native-text.yml')
# GUI.Forms Audio as an outside consumer (runtime-assets.yml). The application jobs
# already decode every prepared asset (tests/audio_tests.cpp), so assets and new games
# don't need this.
AUDIO: tuple[str, ...] = (
    'src/*audio*', 'src/pcm_player.*', 'src/bar_music.*', 'tests/bar_music*', 'tests/portable_audio/**',
    'tests/*audio*', 'games/eggy/**', 'games/fourpegs/**', 'games/atomprobe/**', 'games/switchbox/**',
    'tools/restore_*.py', 'tools/package_common.py', 'tools/ci_changes.py', 'harness.lock',
    '.github/actions/**', '.github/workflows/runtime-assets.yml')
SUITES: tuple[tuple[str, tuple[str, ...]], ...] = (('text', TEXT), ('audio', AUDIO))


def pattern_expression(pattern: str) -> re.Pattern[str]:
    """A path filter as a whole-path regular expression."""
    parts: list[str] = []
    index: int = 0
    while index < len(pattern):
        if pattern.startswith('**', index):
            parts.append('.*')
            index += 2
        elif pattern[index] == '*':
            parts.append('[^/]*')
            index += 1
        elif pattern[index] == '?':
            parts.append('[^/]')
            index += 1
        else:
            parts.append(re.escape(pattern[index]))
            index += 1
    result: re.Pattern[str] = re.compile(''.join(parts))
    return result


def matches(path: str, patterns: tuple[str, ...]) -> bool:
    pattern: str
    for pattern in patterns:
        if pattern_expression(pattern).fullmatch(path) is not None:
            return True
    return False


def changed_paths(base: str, head: str) -> list[str] | None:
    """The paths head changes since its merge base with base, or None when git cannot tell."""
    if not base or not head or set(base) == {'0'}:
        return None
    completed: subprocess.CompletedProcess[str] = subprocess.run(
        ['git', '-C', str(ROOT), 'diff', '--name-only', base + '...' + head], capture_output=True, text=True, check=False)
    if completed.returncode != 0:
        print('Cannot compare ' + base + ' with ' + head + ': ' + completed.stderr.strip(), flush=True)
        return None
    result: list[str] = [line for line in completed.stdout.splitlines() if line]
    return result


def main(argv: list[str] | None = None) -> int:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--base', default='')
    parser.add_argument('--head', default='')
    arguments: argparse.Namespace = parser.parse_args(argv)
    paths: list[str] | None = changed_paths(arguments.base, arguments.head)
    lines: list[str] = []
    name: str
    patterns: tuple[str, ...]
    for name, patterns in SUITES:
        needed: bool = True
        if paths is not None:
            needed = False
            path: str
            for path in paths:
                if matches(path, patterns):
                    needed = True
                    break
        lines.append(name + '=' + ('true' if needed else 'false'))
    if paths is None:
        print('Every suite runs: no comparable base', flush=True)
    else:
        print(str(len(paths)) + ' changed paths', flush=True)
    output: str = os.environ.get('GITHUB_OUTPUT', '')
    line: str
    for line in lines:
        print(line, flush=True)
    if output:
        with open(output, 'a', encoding='utf-8') as stream:
            for line in lines:
                stream.write(line + '\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())
