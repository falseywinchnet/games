#pragma once
// The PlaySuite studio (authoring/playsuite_music) as live cues: its eleven music loops,
// fourteen win and top-score stingers and twenty-two card sounds, each rendered by the
// studio's own code under its runtime name the first time the shell asks for it.
#include "voice_bank.hpp"

#include <span>
#include <string_view>

namespace studio {

// Every cue, by runtime name (music_menu_loop, stinger_win_gems, card_flip_03 ...).
[[nodiscard]] std::span<const voice::Cue> sound_cues();
// Adds every studio cue to voice::Bank::shared(). Call once before the first sound plays.
void register_studio_cues();

}  // namespace studio
