#pragma once
// Autosave: the garden in play (and the moves made there, so it resumes
// exactly), the difficulty New garden deals, what has been cleared at each
// difficulty and how well, and settings. A bounded, versioned, checksummed
// text file replaced atomically.
//
// The format and file name are permanent. Saves from the old sequential book
// (no "difficulty" line) load as they always did and are then upgraded by
// migrate(): see its comment for how old progress maps onto the tiers.
#include "levelset.hpp"

#include <array>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace ct {

struct LevelRecord {
    int best_moves = 0, best_pushes = 0;  // 0: never cleared
};

struct Settings {
    bool sound = true, music = true;
};

struct TierRecord {
    int cleared = 0, perfect = 0;  // gardens cleared at this difficulty; how many in the fewest pushes
};

constexpr int kMaxGardenId = 9999;

struct SaveData {
    int level = 0;                         // table id of the garden in play (-1: a fresh garden, held below)
    std::string history;                   // LURD of the moves made in it so far
    std::map<int, LevelRecord> records;    // table id -> best
    std::string endless_xsb;               // the fresh garden in play (rows joined by '|')
    std::string endless_solution;
    int endless_season = 0, endless_tier = 0, endless_cleared = 0;  // the old book's endless garden (read for migration)
    // since the difficulties (format 2)
    int format = 1;
    int difficulty = 0;                    // what New garden deals: 0 Tutorial, 1 Easy, 2 Medium, 3 Hard
    int garden_tier = 0;                   // the difficulty of the garden in play
    int season = 0;                        // its season (garden.hpp's Season)
    int garden_par = 0;                    // a fresh garden's fewest pushes
    std::string garden_title;              // a fresh garden's name
    std::array<TierRecord, 4> tiers{};
    std::set<int> played;                  // table ids already dealt, so they come round again last
    Settings settings;
};

std::filesystem::path save_path(bool dev);
bool load_save(const std::filesystem::path& path, SaveData& out);
bool write_save(const std::filesystem::path& path, const SaveData& data);

// Upgrades a save from the old sequential book of 243 gardens (format 1) to
// the difficulties, given the garden tables (whose ids are the old book's
// indices). Nothing is lost:
// - the garden in play, and every move made in it, resumes exactly, whether a
//   book garden (found by its id) or the old endless garden;
// - New garden then deals the tier of that garden, so a player halfway through
//   winter carries on at Hard, and one still in the first steps at Tutorial;
// - every best result stays recorded against its garden, counts as a garden
//   cleared at that garden's tier (and as perfect if it matched the par), and
//   its garden is dealt again only after the unplayed ones;
// - gardens cleared in the old endless mode count at the tier matching their
//   season: Spring at Easy, Summer and Autumn at Medium, Winter and Night at Hard;
// - each garden keeps the season it had in the book.
// Returns false (and leaves the save alone) when it is already format 2.
bool migrate(SaveData& data, const std::vector<LevelEntry>& gardens);
int old_book_season(int id);       // the season book garden `id` was shown in
int old_endless_difficulty(int endless_tier);

}  // namespace ct
