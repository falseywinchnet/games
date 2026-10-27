# Audio authoring

This directory preserves the original card/puzzle and Eggy synthesis scripts from the Neo authoring workspace. Their original paths, dependencies, reference analysis and output manifests are preserved in the authoring archive attached to the private `migration-20261001` release. Inspect those scripts before regenerating assets; some still reference the original authoring workspace.

The runtime assets needed by Games are already tracked in this repository. Original WAV masters, the full authoring workspace, toolkit source history, the macOS SDK provenance and the reference app are supplemental release assets. Download with:

```sh
gh release download migration-20261001 --repo falseywinchnet/games --dir migration-reference
```

Personal save snapshots are retained locally and excluded from the GitHub repository and release package. The preserved Switchbox/retired experiments in the archive are authoring history, not instructions to restore those games.
