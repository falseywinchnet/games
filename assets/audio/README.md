# Packaged audio

Two delivered Neo/Claude Opus production batches are integrated here: 174 AAC assets, comprising 16 loops, 22 stingers, and 136 effects. `audio_manifest.json` merges producer metadata; `PACKAGED_SHA256.json` verifies packaged bytes. `AUDIO_MANIFEST.md` and `AUDIO_BATCH_02.md` preserve producer documentation.

The application uses native AAC decoding with exact producer-length PCM loops. See `../../docs/AUDIO_INTEGRATION.md` for playback, verification, source preservation, and scope. Run `tools/package_audio.py` from the project root to reproduce the combined package from the preserved incoming deliveries.

Sticks & Stones was retired from the collection, so its loop, two stingers and ten effects (13 files) are no longer packaged; their rows in `AUDIO_BATCH_02.md` are producer history, and the deliveries remain in `incoming/audio-batch-02`.

Eleven loops and fourteen stingers have since been replaced in place by PlaySuite v2 music from `../../authoring/playsuite_music` (manifest entries marked `"batch": "playsuite-v2"`).
