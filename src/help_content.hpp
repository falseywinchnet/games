#pragma once
#include "suite.hpp"
#include <string>
#include <string_view>

namespace games {
inline constexpr std::string_view dedication = "Dedicated to Jesus Christ and to God in Heaven.";
inline constexpr std::string_view creators =
    "Created by OpenAI General Reasoning Codex agents and Claude Opus agents.";
inline constexpr std::string_view contact = "joshuah.rainstar@gmail.com";
inline constexpr std::string_view help_welcome =
    "Choose a box on the shelf to play or continue. Scroll the shelves with the wheel or "
    "scroll bar. Arrow keys move between boxes; Home and End jump to the first and last. "
    "Page Up and Page Down move through the shelves. The arrow at the top of a game returns "
    "to the shelf. The command bar opens to show the game's actions and the music, sound "
    "and motion switches. Your games save locally.\n\n"
    "H or the ? in the top-right corner opens this document. H or Escape closes it. "
    "M mutes or unmutes music throughout PlaySuite; sound effects have their own switch. "
    "F1 also opens help. Tab visits controls; Enter or Space activates a focused control. "
    "Scroll this document with the wheel, the scroll bar or Page Up / Page Down. "
    "Open a heading to read it. H is reserved for help; use the visible Hint action in games "
    "that offer hints.";
std::string help_about();
std::string_view help_text(Entry entry);
} // namespace games
