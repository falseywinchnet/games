# Packaged audio

Two delivered Neo/Claude Opus production batches are integrated here: 187 AAC assets, comprising 17 loops, 24 stingers, and 146 effects. `audio_manifest.json` merges producer metadata; `PACKAGED_SHA256.json` verifies packaged bytes. `AUDIO_MANIFEST.md` and `AUDIO_BATCH_02.md` preserve producer documentation.

The application uses native AAC decoding with exact producer-length PCM loops. See `../../docs/AUDIO_INTEGRATION.md` for playback, verification, source preservation, and scope. Run `tools/package_audio.py` from the project root to reproduce the combined package from the preserved incoming deliveries.
