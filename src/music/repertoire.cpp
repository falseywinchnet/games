#include "repertoire.hpp"

#include "styles/banjo_band.hpp"
#include "styles/glasshouse.hpp"
#include "styles/island_band.hpp"
#include "styles/koto_garden.hpp"
#include "styles/lounge_band.hpp"
#include "styles/marimba_band.hpp"
#include "styles/maze_band.hpp"
#include "styles/song_band.hpp"

namespace games::music {
namespace {
std::unique_ptr<Composer> make_marimba_band(std::uint32_t seed) { return std::make_unique<MarimbaBand>(seed); }
std::unique_ptr<Composer> make_scruggs(std::uint32_t seed) { return std::make_unique<BanjoBand>(seed, BanjoStyle::scruggs); }
std::unique_ptr<Composer> make_clawhammer(std::uint32_t seed) { return std::make_unique<BanjoBand>(seed, BanjoStyle::clawhammer); }
std::unique_ptr<Composer> make_porch(std::uint32_t seed) { return std::make_unique<BanjoBand>(seed, BanjoStyle::porch); }
std::unique_ptr<Composer> make_maze_run(std::uint32_t seed) { return std::make_unique<MazeBand>(seed, MazeStyle::maze_run); }
std::unique_ptr<Composer> make_bonus_round(std::uint32_t seed) { return std::make_unique<MazeBand>(seed, MazeStyle::bonus_round); }
std::unique_ptr<Composer> make_turbo(std::uint32_t seed) { return std::make_unique<MazeBand>(seed, MazeStyle::turbo); }
std::unique_ptr<Composer> make_bossa_lounge(std::uint32_t seed) { return std::make_unique<LoungeBand>(seed, LoungeStyle::bossa_lounge); }
std::unique_ptr<Composer> make_smooth_jazz(std::uint32_t seed) { return std::make_unique<LoungeBand>(seed, LoungeStyle::smooth_jazz); }
std::unique_ptr<Composer> make_swing_combo(std::uint32_t seed) { return std::make_unique<LoungeBand>(seed, LoungeStyle::swing_combo); }
std::unique_ptr<Composer> make_game_show(std::uint32_t seed) { return std::make_unique<LoungeBand>(seed, LoungeStyle::game_show); }
std::unique_ptr<Composer> make_cocktail_organ(std::uint32_t seed) { return std::make_unique<LoungeBand>(seed, LoungeStyle::cocktail_organ); }
std::unique_ptr<Composer> make_steel_calypso(std::uint32_t seed) { return std::make_unique<LoungeBand>(seed, LoungeStyle::steel_calypso); }
std::unique_ptr<Composer> make_on_hold(std::uint32_t seed) { return std::make_unique<LoungeBand>(seed, LoungeStyle::on_hold); }
std::unique_ptr<Composer> make_hawaiian_exotica(std::uint32_t seed) { return std::make_unique<LoungeBand>(seed, LoungeStyle::hawaiian_exotica); }
std::unique_ptr<Composer> make_musette_waltz(std::uint32_t seed) { return std::make_unique<LoungeBand>(seed, LoungeStyle::musette_waltz); }
std::unique_ptr<Composer> make_cha_cha(std::uint32_t seed) { return std::make_unique<LoungeBand>(seed, LoungeStyle::cha_cha); }
std::unique_ptr<Composer> make_velvet_prize(std::uint32_t seed) { return std::make_unique<SongBand>(seed, velvet_prize()); }
std::unique_ptr<Composer> make_walk_on(std::uint32_t seed) { return std::make_unique<SongBand>(seed, walk_on()); }
std::unique_ptr<Composer> make_glasshouse(std::uint32_t seed) { return std::make_unique<Glasshouse>(seed); }
std::unique_ptr<Composer> make_island_band(std::uint32_t seed) { return std::make_unique<IslandBand>(seed); }
std::unique_ptr<Composer> make_koto_day(std::uint32_t seed) { return std::make_unique<KotoGarden>(seed, false); }
std::unique_ptr<Composer> make_koto_night(std::uint32_t seed) { return std::make_unique<KotoGarden>(seed, true); }
struct Entry {
    std::string_view name;
    std::unique_ptr<Composer> (*make)(std::uint32_t seed);
    double level;  // the gain its game plays it at
};
constexpr Entry entries[] = {
    {"marimba_band", make_marimba_band, .85},      // The Paradoxiary
    {"koto_garden", make_koto_day, 1.0},           // Sudoku by day
    {"koto_garden_night", make_koto_night, 1.0},   // and by night
    {"glasshouse", make_glasshouse, 1.0},          // Nature Cube
    {"island_band", make_island_band, .25},        // Stillwater
    {"bluegrass", make_scruggs, 1.0},              // Mowing: Scruggs banjo, the lead passed to a fiddle
    {"clawhammer", make_clawhammer, 1.0},          // an open-back banjo frailing old-time tunes
    {"porch_banjo", make_porch, 1.0},              // Scruggs at an easy pace, a quieter band
    {"maze_run", make_maze_run, 1.0},              // Maze: synth rock
    {"bonus_round", make_bonus_round, 1.0},        // a romp
    {"turbo", make_turbo, 1.0},                    // a funk groove
    {"bossa_lounge", make_bossa_lounge, 1.0},  // the lounge bands: forks of the marimba band, in G
    {"smooth_jazz", make_smooth_jazz, 1.0},
    {"swing_combo", make_swing_combo, 1.0},
    {"game_show", make_game_show, 1.0},
    {"cocktail_organ", make_cocktail_organ, 1.0},
    {"steel_calypso", make_steel_calypso, 1.0},
    {"on_hold", make_on_hold, 1.0},
    {"hawaiian_exotica", make_hawaiian_exotica, 1.0},
    {"musette_waltz", make_musette_waltz, 1.0},
    {"cha_cha", make_cha_cha, 1.0},
    {"velvet_prize", make_velvet_prize, 1.0},      // written songs: game-show themes
    {"walk_on", make_walk_on, 1.0},
};
}  // namespace

std::unique_ptr<Composer> make_score(std::string_view style, std::uint32_t seed) {
    for (const Entry& e : entries)
        if (e.name == style) return e.make(seed);
    return nullptr;
}

double score_level(std::string_view style) {
    for (const Entry& e : entries)
        if (e.name == style) return e.level;
    return 0;
}

std::vector<std::string_view> score_names() {
    std::vector<std::string_view> names;
    for (const Entry& e : entries) names.push_back(e.name);
    return names;
}

}  // namespace games::music
