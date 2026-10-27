#pragma once
// Everything Nature Cube remembers between runs, and the files that hold it.
//
// The current file is cube-v3.txt: a magic line, "key=value" lines, and a checksum line.
// Earlier versions kept the game in cube-v2.txt, in the shared puzzle engine's format
// (RAINSTAR_PUZZLE 1, four-by-four boards, an optional fill-every-tile rule on Hard).
// import_legacy reads that file into a session, so a game in progress resumes exactly
// where it was and is won, as the rules now say, once every pair is joined. The old file
// is left untouched.
#include "cube.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ps_cube {

inline constexpr const char* save_magic = "NATURE_CUBE3";
inline constexpr std::size_t save_limit_bytes = 65536;
inline constexpr std::size_t score_table_size = 10;

struct TopScore {
    std::string name;
    int strokes = 0;
};

struct Session {
    Puzzle puzzle;
    Play play;
    int next_level = 0;        // the level of the next new board
    bool portals_met = false;  // the player has had a board with portals
    std::vector<TopScore> scores;  // fewest strokes first
    std::string player = "Player";
    // A finished board's result waiting for a name, or -1. Cleared when entered or declined.
    int pending = -1;
};

// A printable name of one to 24 characters with something visible in it.
[[nodiscard]] bool valid_name(const std::string& name);
// True when a finished board's strokes would enter the table.
[[nodiscard]] bool qualifies(const std::vector<TopScore>& scores, int strokes);
// Enters a result, keeping the table sorted and at most ten long. False if it does not fit.
bool record(std::vector<TopScore>& scores, const std::string& name, int strokes);

// Replays a play's lines through the rules onto a fresh play. False when any step is
// refused: a save can never produce a position the rules could not.
[[nodiscard]] bool replay(const Puzzle& puzzle, const std::vector<std::vector<int>>& lines,
                          int strokes, Play& destination);

[[nodiscard]] std::string encode_session(const Session& session);
// Leaves `destination` untouched unless the whole text is valid.
[[nodiscard]] bool decode_session(const std::string& text, Session& destination);

// The envelope: magic line, body, "check=" line with a 64-bit FNV-1a of the body.
[[nodiscard]] std::uint64_t checksum(const std::string& body);
[[nodiscard]] std::string seal(const std::string& body);
[[nodiscard]] bool unseal(const std::string& sealed, std::string& body);
bool write_save(const std::filesystem::path& path, const std::string& body);
[[nodiscard]] bool read_save(const std::filesystem::path& path, std::string& body);

// Reads a cube-v2.txt (or cube-v1.txt) file of the shared puzzle engine. With `board`
// false only the top scores and player name are taken. Leaves `destination` untouched on
// failure.
[[nodiscard]] bool import_legacy(const std::string& file_text, bool board, Session& destination);
[[nodiscard]] bool read_file(const std::filesystem::path& path, std::string& text);

}  // namespace ps_cube
