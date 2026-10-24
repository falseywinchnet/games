#pragma once
// The gnome's voice, rendered live: audio_src/gnome_voice.mjs and the parts of the
// monk choir's HDR voice it performs on (a physical glottis driving a Kelly-Lochbaum
// vocal tract with a nasal branch and a subglottal airway, glottal and constriction
// turbulence, closure impacts and cross-modes), ported from the JavaScript expression
// by expression. JavaScript's Math functions are V8's fdlibm ports, so they are ported
// too (gnome_math.hpp): natively on macOS the clips equal the ones Node renders there
// bit for bit; elsewhere only Math.pow (the platform's own) may differ in its last bit.
#include "voice_bank.hpp"

#include <span>

namespace mm {

// mm_gnome_hoo, mm_gnome_hoo2, mm_gnome_giggle, mm_gnome_giggle2 and mm_gnome_nyah:
// mono masters (the script writes them as left = right), each the float the script
// rounds to 16 bits: the voice levelled to -17 dBFS RMS, faded and soft-limited.
[[nodiscard]] std::span<const voice::Cue> gnome_cues();

}  // namespace mm
