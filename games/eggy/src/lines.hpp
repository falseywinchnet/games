#pragma once
// Eggy's line bank: assets/lines/<category>.txt, one line per row.
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace eggy {

class LineBank {
public:
    void load(const std::string& dir);
    std::string pick(const std::string& category);  // avoids recent repeats
    size_t total() const;
    bool has(const std::string& category) const { return lines_.count(category) && !lines_.at(category).empty(); }

private:
    std::map<std::string, std::vector<std::string>> lines_;
    std::deque<std::uint64_t> recent_;
    std::uint64_t rng_ = 0x5EEDULL;
};

}  // namespace eggy
