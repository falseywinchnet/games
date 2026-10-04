# Deliver a game

For the repository owner's authorized workflow, delivery is a tested game folder
and one push to `main`. GitHub builds and publishes the new revision automatically.
A pull request is optional unless the person or repository permissions require it.

## Finish the game

Follow the development checks in [AGENTS.md](../AGENTS.md): prove the rules,
inspect the presentation at the supported sizes, exercise the actual standalone
window, then exercise the same game in the suite. Use the authoring gate with the
native executable and finite script:

```sh
python3 new-games/tools/check_game.py <id> --fetch-toolkit --application <games-executable> --script /path/to/play.script
```

Fix failures. Keep the inspected pictures in the game's `screens/` directory;
`shrink_png.py` can compress them. Keep `HANDOFF.md` short: what you actually tested,
real limits such as unjudged sound quality, and relevant provenance or decisions.
This is a game record, not a release report. Existing publication authorization
remains valid; do not introduce another approval ceremony.

## Commit the folder and push

For an authorized checkout based on current `main`, with only the intended game
commits ahead of it:

```sh
git add vendor/<id>
git diff --cached --stat
git commit -m "Add <Title> to the shelf"
git push origin HEAD:main
```

Keep builds and personal saves out of the commit. A concurrent update may cause
the push to be rejected: fetch, reconcile the new commits and any ID collision,
rerun affected game checks, and push normally. Do not force-push `main`.

If a PR is requested or write access is unavailable, push the feature branch and
open a PR instead. Its merge to `main` starts deployment. A feature-branch push
by itself does not publish a release.

## GitHub does the release work

The application workflow automatically discovers the new folder, prepares its
assets, updates the shelf/help/count, checks style and all game contracts, builds
Windows x64, macOS arm64 and both Linux architectures, tests the installers, and
publishes one version containing all games. It creates the version, immutable
tag and checksums. A failed build leaves the last published release available.

Do not manually edit registration files, inventories, counts, versions, packaging
or release notes. Do not repeat the entire platform test matrix locally, download
CI artifacts, create a release, or poll every job as part of ordinary game authorship.
The pipeline owns that work. Link the
[application workflow](https://github.com/falseywinchnet/games/actions/workflows/applications.yml)
and report that automatic publication is running. Fix a reported failure in the
game; report an unrelated infrastructure failure without weakening its tests.

When the person explicitly requests confirmation that deployment finished,
verify the published release before reporting success. Otherwise a successful
push is the author's handoff to the automatic publication pipeline.
