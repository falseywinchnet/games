#pragma once
// The save envelope every PlaySuite game uses: a magic line with the format
// version, a bounded text body, and a checksum line. Files are replaced
// atomically (written beside the target, then renamed), so a crash or a full
// disk leaves the previous save intact. A failed or damaged read reports false
// and the game starts fresh; it never guesses.
#include <cstdint>
#include <filesystem>
#include <string>

namespace tg {

inline constexpr const char* save_magic = "TEMPLATEGAME1";
inline constexpr std::size_t save_limit_bytes = 65536;

[[nodiscard]] std::uint64_t checksum(const std::string& body);
// Wraps a body in the envelope.
[[nodiscard]] std::string seal(const std::string& body);
// Recovers the body. False when the magic, size or checksum is wrong.
[[nodiscard]] bool unseal(const std::string& sealed, std::string& body);
// False when the file cannot be written; the previous file is then unchanged.
bool write_save(const std::filesystem::path& path, const std::string& body);
[[nodiscard]] bool read_save(const std::filesystem::path& path, std::string& body);

}  // namespace tg
