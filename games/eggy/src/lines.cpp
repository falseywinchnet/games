#include "lines.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>

namespace eggy {

void LineBank::load(const std::string& dir) {
    lines_.clear();
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
        if (e.path().extension() != ".txt") continue;
        std::ifstream f(e.path());
        std::string line;
        auto& v = lines_[e.path().stem().string()];
        while (std::getline(f, line))
            if (!line.empty() && line.size() <= 80) v.push_back(line);
    }
    rng_ ^= static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
}

size_t LineBank::total() const {
    size_t n = 0;
    for (const auto& [k, v] : lines_) n += v.size();
    return n;
}

std::string LineBank::pick(const std::string& category) {
    auto it = lines_.find(category);
    if (it == lines_.end() || it->second.empty()) return {};
    const auto& v = it->second;
    for (int tries = 0; tries < 12; ++tries) {
        rng_ ^= rng_ << 13; rng_ ^= rng_ >> 7; rng_ ^= rng_ << 17;
        const size_t i = static_cast<size_t>(rng_ % v.size());
        const std::uint64_t key = std::hash<std::string>()(v[i]);
        if (std::find(recent_.begin(), recent_.end(), key) != recent_.end()) continue;
        recent_.push_back(key);
        if (recent_.size() > 400) recent_.pop_front();
        return v[i];
    }
    return v[static_cast<size_t>(rng_ % v.size())];
}

}  // namespace eggy
