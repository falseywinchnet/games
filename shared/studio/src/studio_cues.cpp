#include "studio_cues.hpp"

#include "studio_card_sfx.hpp"
#include "studio_mix.hpp"
#include "studio_songs.hpp"

#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>

namespace studio {
namespace {

// The studio's songs and stingers in its own registry order (songs_common.cpp, stingers.cpp)
// and its card sounds (card_sfx.cpp), under the names the games play.
constexpr std::array<std::string_view, 47> names = {
    "music_menu_loop",
    "music_klondike_loop",
    "music_spider_loop",
    "music_freecell_loop",
    "music_hearts_loop",
    "music_sudoku_day_loop",
    "music_sudoku_night_loop",
    "music_gems_loop",
    "music_nature_cube_loop",
    "music_untangle_loop",
    "music_puzzle_solve_loop",
    "stinger_win_klondike",
    "stinger_win_spider",
    "stinger_win_freecell",
    "stinger_win_hearts",
    "stinger_win_sudoku",
    "stinger_topscore_sudoku",
    "stinger_win_gems",
    "stinger_topscore_gems",
    "stinger_win_nature_cube",
    "stinger_topscore_nature_cube",
    "stinger_win_untangle",
    "stinger_topscore_untangle",
    "stinger_win_puzzle_solve",
    "stinger_topscore_puzzle_solve",
    "card_pickup_01",
    "card_pickup_02",
    "card_pickup_03",
    "card_pickup_04",
    "card_place_01",
    "card_place_02",
    "card_place_03",
    "card_place_04",
    "card_flip_01",
    "card_flip_02",
    "card_flip_03",
    "card_flip_04",
    "card_foundation_01",
    "card_foundation_02",
    "card_foundation_03",
    "card_foundation_04",
    "card_deal_01",
    "card_deal_02",
    "card_deal_03",
    "card_deal_04",
    "card_shuffle_01",
    "card_shuffle_02",
};

voice::Stereo widen(const Stereo& s) {
    voice::Stereo result{};
    result.left.assign(s.L.begin(), s.L.end());
    result.right.assign(s.R.begin(), s.R.end());
    return result;
}

Stereo render_named(std::string_view name) {
    if (name.starts_with("card_")) {
        return renderCardSfx(name);
    }
    for (const SongFactory& factory : allSongs()) {
        if (factory.id == name) {
            const Song song = factory.make();
            return renderSong(song);
        }
    }
    throw std::logic_error("no studio piece called " + std::string(name));
}

template <std::size_t I>
voice::Stereo render_cue() {
    return widen(render_named(names[I]));
}

template <std::size_t... I>
constexpr std::array<voice::Cue, sizeof...(I)> make_cues(std::index_sequence<I...>) {
    return {voice::Cue{.name = names[I], .stereo = render_cue<I>}...};
}

constexpr std::array<voice::Cue, names.size()> cues = make_cues(std::make_index_sequence<names.size()>{});

}  // namespace

std::span<const voice::Cue> sound_cues() {
    return cues;
}

void register_studio_cues() {
    voice::Bank::shared().add(cues);
}

}  // namespace studio
