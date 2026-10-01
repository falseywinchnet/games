# Delivered audio integration

Both original music/effect batches from the user's Neo/Claude Opus workflow are integrated. Producer documentation is preserved as `assets/audio/AUDIO_MANIFEST.md` and `AUDIO_BATCH_02.md`; the merged `audio_manifest.json` retains original names, sample counts, and producer measurements. `PACKAGED_SHA256.json` describes the 187 packaged AAC files: 17 loops, 24 stingers, and 146 effects. Some tracks support the separately planned Catching Thieves module.

`tools/package_audio.py` combines both delivered batches without overwriting one with the other, preserves producer manifests, verifies source availability, and writes package hashes. Runtime music aliases map Solitaire to Klondike and Nature Cube to nature_cube. Sudoku switches between day and night music. Numbered action variants cycle; Gems' match sounds follow cascade depth, with bomb/star/hypercube cues for special clears.

The macOS runtime decodes each AAC music file to PCM and trims the decoded buffer to the producer's exact loop sample count before scheduling it repeatedly through AVAudioEngine/AVAudioPlayerNode. It does not repeat AAC encoder padding. Music remains on the same buffer across ordinary saves and pauses for terminal stingers. Music/effects have independent toggles; initial gains are 0.4 and 1.0 respectively. Native effects use AVAudioPlayer. The originals also include Opus encodes; AAC is the native packaged playback format. Claude Opus production and the Opus codec are distinct.

Automated validation decodes all 187 AAC assets, checks finite samples, checks all seventeen exact loop frame counts, and compares offline-rendered loop joins with repeated expected PCM buffers (absolute tolerance 1e-5). This verifies sample scheduling, not a subjective listening review or a first-sample comparison against every WAV master. Producer loudness/seam measurements remain producer measurements.

Batch-one duplicate WAV masters on the M4 were removed only after explicit user approval during the storage incident. The original masters remain on the Neo; delivered compressed files and manifests remain preserved. No reference-song recordings are packaged. Original/generated provenance is not a separate legal clearance opinion.

## Eggy delivery

The separate `game-eggy-01` handoff adds 44 AAC files and five seamless WAV ambience beds. These are copied unchanged into the same assets directory and played by Eggy's own engine, gated by the cabinet's Music/Sound masters and foreground visibility. The total runtime decode check now covers 236 files (the original 187 plus Eggy's 49). The exact 17-loop seam verification above applies to the two original cabinet audio batches; it is not an additional loop-seam claim for Eggy. See `EGGY_INTEGRATION.md` for package integrity and native integration details.
