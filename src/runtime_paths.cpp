#include "runtime_paths.hpp"
#include <cstdlib>
#include <stdexcept>
#include <vector>
namespace games {
namespace {
std::filesystem::path assets{};
std::filesystem::path environment_path(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr ? std::filesystem::path(value) : std::filesystem::path{};
}
}
void initialize_assets(const char* executable) {
    std::filesystem::path override_path = environment_path("GAMES_ASSET_DIR");
    if (!override_path.empty()) {
        assets = std::filesystem::absolute(override_path);
        return;
    }
    std::filesystem::path program(executable);
    if (!program.has_parent_path() && !std::filesystem::exists(program)) {
        const char* raw = std::getenv("PATH");
        std::string search = raw != nullptr ? raw : "";
#ifdef _WIN32
        constexpr char separator = ';';
        if (!program.has_extension()) { program += ".exe"; }
#else
        constexpr char separator = ':';
#endif
        std::size_t start = 0;
        while (start <= search.size()) {
            std::size_t end = search.find(separator, start);
            if (end == std::string::npos) { end = search.size(); }
            std::filesystem::path candidate = std::filesystem::path(search.substr(start, end - start)) / program;
            if (std::filesystem::is_regular_file(candidate)) { program = candidate; break; }
            if (end == search.size()) { break; }
            start = end + 1;
        }
    }
    std::filesystem::path directory = std::filesystem::absolute(program).parent_path();
    const std::filesystem::path candidates[] = {directory / "assets", directory.parent_path() / "Resources/assets"};
    for (const std::filesystem::path& candidate : candidates) {
        if (std::filesystem::is_regular_file(candidate / "sudoku/engine.js")) {
            assets = candidate;
            return;
        }
    }
    throw std::runtime_error("Games assets are missing beside the executable.");
}
std::string asset_directory() {
    if (!assets.empty()) { return assets.string(); }
    std::filesystem::path override_path = environment_path("GAMES_ASSET_DIR");
    if (!override_path.empty()) { return override_path.string(); }
    throw std::runtime_error("Games assets were not initialized.");
}
std::filesystem::path state_directory() {
#ifdef _WIN32
    const wchar_t* isolated = _wgetenv(L"GAMES_STATE_DIR");
    if (isolated != nullptr && isolated[0] != 0) { return std::filesystem::path(isolated); }
    const wchar_t* roaming = _wgetenv(L"APPDATA");
    if (roaming != nullptr && roaming[0] != 0) { return std::filesystem::path(roaming) / L"Rainstar/Games"; }
#else
    std::filesystem::path isolated = environment_path("GAMES_STATE_DIR");
    if (!isolated.empty()) { return isolated; }
    std::filesystem::path home = environment_path("HOME");
#ifdef __APPLE__
    if (!home.empty()) { return home / "Library/Application Support/Rainstar/Games"; }
#else
    std::filesystem::path state = environment_path("XDG_STATE_HOME");
    if (!state.empty() && state.is_absolute()) { return state / "rainstar/games"; }
    if (!home.empty()) { return home / ".local/state/rainstar/games"; }
#endif
#endif
    throw std::runtime_error("No per-user Games save directory is available.");
}
}
