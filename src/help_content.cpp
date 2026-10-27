#include "help_content.hpp"
#include "game_module.hpp"
namespace games {
std::string_view help_text(Entry entry) {
    return valid_entry(entry) ? game_descriptor(entry).help : "";
}
std::string help_about() {
    return "PlaySuite is a collection of " + std::to_string(entry_count) +
           " games built with GUI.Forms. The card faces and backs are original artwork; "
           "the felt uses Plan Paint's fiber and light renderer. Music, effects and game "
           "data are stored locally. There are no accounts, advertisements or network "
           "opponents. Your saved games stay on your computer.\n\n"
           "When reporting a problem, include the game, PlaySuite version, operating system "
           "and the steps that led to it. A screenshot and a copy of the affected save can "
           "help reproduce it.";
}
} // namespace games
