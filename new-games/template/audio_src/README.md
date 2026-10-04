# Sound sources

Every sound in PlaySuite is made here, from code: no samples, no recordings, no
borrowed tunes. Keep the scripts that render this game's music and effects in this
folder so anyone can rebuild them. `make_sfx.py` is a small standard-library
starting point; for richer instruments, copy
`vendor/zenconstruction/audio_src/engine/synth.py` into this folder (it needs NumPy)
and write the score beside it. See `new-games/guide/06-look-sound-words.md`.

Render into the repository's `assets/audio/` with this game's file prefix, then
run `wire_shelf.py` again so the audio inventory counts the new files.
