#pragma once
// The save envelope every PlaySuite game uses: a magic line with the format
// version, a bounded text body, and a checksum line. PlaySuite stores it
// (games::save_game_data): the game never opens files itself, and an entry is
// replaced atomically, so a crash or a full disk leaves the previous save
// intact. A failed or damaged read reports false and the game starts fresh; it
// never guesses.
#include <cstdint>
#include <string>
#include <string_view>

namespace tg {

inline constexpr const char* save_magic = "TEMPLATEGAME1";
inline constexpr std::size_t save_limit_bytes = 65536;
// This game's id (GAME.json), under which PlaySuite keeps its saves.
inline constexpr std::string_view save_game = "templategame";

[[nodiscard]] std::uint64_t checksum(const std::string& body);
// Wraps a body in the envelope.
[[nodiscard]] std::string seal(const std::string& body);
// Recovers the body. False when the magic, size or checksum is wrong.
[[nodiscard]] bool unseal(const std::string& sealed, std::string& body);
// `name` is the save's name ("templategame-v1.txt"). False when it cannot be
// written; the previous save is then unchanged.
bool write_save(std::string_view name, const std::string& body);
[[nodiscard]] bool read_save(std::string_view name, std::string& body);

}  // namespace tg
