#!/usr/bin/env python3
"""Packs the runtime files of each browser exhibit (tools/web/exhibits.json).

    pack_exhibits.py --runtime <prepared runtime> --output <web build>/packs --packager <file_packager> [game...]

Writes packs/base.{data,js} and packs/<game>.{data,js}; the page loads base, then
the game's pack, and mounts both at /assets. Audio never ships (the web build
generates every sound), so .ogg/.wav/.m4a are refused even if a glob matches.
"""
import argparse
import json
import shutil
import subprocess
import tempfile
from pathlib import Path

AUDIO = {'.ogg', '.wav', '.m4a'}


def collect(runtime: Path, globs: list[str]) -> list[Path]:
    files = set()
    for pattern in globs:
        files.update(path for path in runtime.glob(pattern) if path.is_file())
    return sorted(path for path in files if path.suffix.lower() not in AUDIO)


def pack(name: str, files: list[Path], runtime: Path, output: Path, packager: str) -> int:
    if not files:
        # A game that needs only the base pack still gets a (empty) script to load.
        (output / f'{name}.data').unlink(missing_ok=True)
        (output / f'{name}.js').write_text(f'// {name}: every runtime file it opens is in base.js\n', encoding='utf-8')
        return 0
    with tempfile.TemporaryDirectory() as staging:
        root = Path(staging) / 'assets'
        for path in files:
            target = root / path.relative_to(runtime)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, target)
        root.mkdir(exist_ok=True)
        subprocess.run([packager, f'{name}.data', '--preload', f'{root}@/assets',
                        f'--js-output={name}.js', '--no-node'], cwd=output, check=True)
    return sum(path.stat().st_size for path in files)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument('--runtime', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--packager', required=True)
    parser.add_argument('--manifest', type=Path, default=Path(__file__).with_name('exhibits.json'))
    parser.add_argument('games', nargs='*')
    options = parser.parse_args()
    manifest = json.loads(options.manifest.read_text(encoding='utf-8'))
    runtime = options.runtime.resolve()
    options.output.mkdir(parents=True, exist_ok=True)
    games = options.games or sorted(manifest['games'])
    unknown = [game for game in games if game not in manifest['games']]
    if unknown:
        raise SystemExit('unknown exhibit: ' + ', '.join(unknown))
    size = pack('base', collect(runtime, manifest['base']), runtime, options.output, options.packager)
    print(f'base: {size / 1048576:.1f} MB')
    for game in games:
        files = collect(runtime, manifest['games'][game])
        size = pack(game, files, runtime, options.output, options.packager)
        print(f'{game}: {len(files)} files, {size / 1048576:.1f} MB')


if __name__ == '__main__':
    main()
