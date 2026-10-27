#include "save.hpp"

#include "game_data.hpp"

#include <optional>

namespace tg {

std::uint64_t checksum(const std::string& body) {
    // FNV-1a, 64 bit.
    std::uint64_t hash = 14695981039346656037ULL;
    for (std::size_t index = 0; index < body.size(); ++index) {
        hash ^= static_cast<unsigned char>(body[index]);
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::string seal(const std::string& body) {
    std::string sealed = std::string(save_magic) + "\n" + body;
    sealed += "check=" + std::to_string(checksum(body)) + "\n";
    return sealed;
}

bool unseal(const std::string& sealed, std::string& body) {
    const std::string header = std::string(save_magic) + "\n";
    if (sealed.size() > save_limit_bytes || sealed.rfind(header, 0) != 0) {
        return false;
    }
    const std::size_t check_at = sealed.rfind("check=");
    if (check_at == std::string::npos || check_at < header.size()) {
        return false;
    }
    const std::string candidate = sealed.substr(header.size(), check_at - header.size());
    const std::string expected = std::to_string(checksum(candidate)) + "\n";
    if (sealed.substr(check_at + 6) != expected) {
        return false;
    }
    body = candidate;
    return true;
}

bool write_save(std::string_view name, const std::string& body) {
    const std::string sealed = seal(body);
    if (sealed.size() > save_limit_bytes) {
        return false;
    }
    return games::save_game_data(save_game, name, sealed);
}

bool read_save(std::string_view name, std::string& body) {
    const std::optional<std::string> sealed = games::load_game_data(save_game, name);
    if (!sealed) {
        return false;
    }
    const bool ok = unseal(*sealed, body);
    return ok;
}

}  // namespace tg
