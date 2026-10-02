#pragma once
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace games_test {
inline std::filesystem::path scratch_directory(const char* prefix) {
    const std::chrono::steady_clock::duration now = std::chrono::steady_clock::now().time_since_epoch();
    std::filesystem::path root = std::filesystem::temp_directory_path();
    for (int attempt = 0; attempt < 100; ++attempt) {
        std::string name = std::string(prefix) + std::to_string(now.count()) + "-" + std::to_string(attempt);
        std::filesystem::path candidate = root / name;
        if (std::filesystem::create_directory(candidate)) {
            return candidate;
        }
    }
    throw std::runtime_error("Cannot allocate an isolated test directory.");
}
inline void isolate_saves(const std::filesystem::path& path) {
#ifdef _WIN32
    int status = _wputenv_s(L"GAMES_STATE_DIR", path.c_str());
#else
    int status = setenv("GAMES_STATE_DIR", path.c_str(), 1);
#endif
    if (status != 0) {
        throw std::runtime_error("Cannot isolate test save data.");
    }
}
}
