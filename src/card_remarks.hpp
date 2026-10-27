#pragma once
// What is murmured around the table when a business card is passed round: the remarks that
// rise and fade about the card in the help book. Each is made fresh from a small grammar:
// families of lines whose {slots} are filled at random from lists whose entries may hold
// slots of their own, so "Observe the {stock}." comes out as "Observe the restrained eggshell
// stock." one time and "Observe the quietly assertive bone cotton." the next. A line said
// lately is not said again.
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>

namespace games {

class CardRemarks {
  public:
    explicit CardRemarks(std::uint32_t seed);
    // The next remark, its sentences begun with capitals.
    [[nodiscard]] std::string next();

  private:
    std::uint32_t state_;
    std::deque<std::string> recent_;  // the last few said, oldest first
    [[nodiscard]] std::uint32_t random();
    [[nodiscard]] std::string expand(std::string_view text, int depth);
};

}  // namespace games
