#!/usr/bin/env python3
"""Compatibility command: validate automatic folder discovery without editing the shell.

CMake reads vendor/*/GAME.json and GAMES_EXTRA_GAME_DIRS, generating its registry
inside the build directory. This command checks the same catalog and asset
contract. It never modifies source, counts, saved ids, covers, help or build files.
"""
import argparse
import sys
import kitlib


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("id", help="game id or path to an external game folder")
    parser.add_argument("--check", action="store_true", help="retained for compatibility; validation is always read-only")
    args = parser.parse_args()
    directory = kitlib.game_dir(args.id)
    try:
        games = kitlib.catalog([directory], include_disabled=True)
        game = next(game for game in games if game["directory"] == directory.resolve())
        if not game.get("enabled", True):
            raise ValueError("This game is disabled; its permanent id remains reserved")
        sys.path.insert(0, str(kitlib.REPO / "tools"))
        from prepare_portable_assets import asset_inventory
        inventory = asset_inventory(kitlib.REPO / "assets", [directory])
    except (ValueError, OSError, StopIteration) as error:
        parser.exit(1, str(error) + "\n")
    print(f'{game["id"]}: discovered as permanent entry {game["entry_id"]}; {len(inventory["audio"])} total audio assets validated.')
    print("No central wiring edits. Reconfigure CMake to regenerate the registry in the build directory.")
    if directory.parent != kitlib.VENDOR:
        print(f'Use -DGAMES_EXTRA_GAME_DIRS="{directory}" for this external folder.')
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
