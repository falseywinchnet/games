#pragma once
#include "suite.hpp"
#include <string_view>

namespace games {
inline constexpr std::string_view dedication = "Dedicated to Jesus Christ and to God in Heaven.";
inline constexpr std::string_view creators =
    "Created by OpenAI General Reasoning Codex agents and Claude Opus agents.";
inline constexpr std::string_view contact = "joshuah.rainstar@gmail.com";
inline constexpr std::string_view help_welcome =
    "Choose a box on the shelf to play or continue. The arrow at the top of a game returns "
    "to the shelf. The command bar opens to show the game's actions and the music, sound "
    "and motion switches. Your games save locally.\n\n"
    "H or the ? in the top-right corner opens this document. H or Escape closes it. "
    "M mutes or unmutes music throughout PlaySuite; sound effects have their own switch. "
    "F1 also opens help. Tab visits controls; Enter or Space activates a focused control. "
    "Scroll this document with the wheel, the scroll bar or Page Up / Page Down. "
    "Open a heading to read it. H is reserved for help; use the visible Hint action in games "
    "that offer hints.";
inline constexpr std::string_view help_about =
    "Dedicated to Jesus Christ and to God in Heaven.\n\n"
    "Created by OpenAI General Reasoning Codex agents and Claude Opus agents.\n"
    "Author: Astra\nSponsor: Rainstar\nContact: joshuah.rainstar@gmail.com\n\n"
    "PlaySuite is a collection of eighteen games built with GUI.Forms. The card faces and "
    "backs are original artwork; the felt uses Plan Paint's fiber and light renderer. "
    "Music, effects and game data are stored locally. There are no accounts, advertisements "
    "or network opponents. Your saved games stay on your computer.\n\n"
    "When reporting a problem, include the game, PlaySuite version, operating system and "
    "the steps that led to it. A screenshot and a copy of the affected save can help reproduce it.";
inline constexpr std::string_view koi_sets =
    "Five Brights: all five, 10 points. Four Brights without the Rain Man: 8. Rainy Four "
    "Brights: 7. Three Brights without the Rain Man: 5.\n\n"
    "Viewing the Blossoms: Curtain and Sake Cup, 5. Viewing the Moon: Full Moon and Sake Cup, "
    "5.\n\n"
    "Boar, Deer and Butterflies: 5, plus 1 for each extra animal. Animals: any five, 1, "
    "plus 1 for each extra.\n\n"
    "Red Poem Ribbons (pine, plum, cherry): 5. Blue Ribbons (peony, chrysanthemum, maple): 5. "
    "Both: 10. Each extra ribbon adds 1. Ribbons: any five, 1, plus 1 for each extra.\n\n"
    "Plains: any ten, 1, plus 1 for each extra. The Sake Cup also counts as a plain.\n\n"
    "The Brights are Crane (January), Curtain (March), Full Moon (August), Rain Man "
    "(November) and Phoenix (December). Each card's label names its month and kind.";
std::string_view help_text(Entry entry);
} // namespace games
