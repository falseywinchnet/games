#include "storage.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unistd.h>
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
int main() {
    std::filesystem::path root =
        std::filesystem::temp_directory_path() / ("games-storage-test-" + std::to_string(getpid()));
    std::filesystem::path path = root / "save.txt";
    games::Cabinet source;
    source.active = 2;
    source.back = 3;
    source.reduced = true;
    for (int i = 0; i < 4; ++i) {
        source.started[i] = true;
        source.games[i].deal(static_cast<games::Kind>(i), 1234 + i);
    }
    source.player_name = "Ada \"Ace\"";
    source.result_recorded[2] = true;
    games::State finished = source.games[2].state;
    finished.over = true;
    for (int i = 0; i < 15; ++i) {
        finished.moves = 100 - i;
        require(games::add_top_score(source.top_scores, finished, "Player " + std::to_string(i)),
                "qualifying result");
    }
    require(source.top_scores[5].size() == 10, "only top ten retained");
    require(source.top_scores[5].front().value == 86 && source.top_scores[5].back().value == 95,
            "low move counts rank first");
    finished.moves = 999;
    require(!games::qualifies(source.top_scores, finished), "nonqualifying result excluded");
    finished.kind = games::Kind::solitaire;
    finished.score = 300;
    require(games::add_top_score(source.top_scores, finished, source.player_name),
            "solitaire high score");
    finished.score = 400;
    require(games::add_top_score(source.top_scores, finished, "Bea"), "higher score accepted");
    require(source.top_scores[0].front().value == 400, "higher solitaire score ranks first");
    finished.draw_count = 3;
    require(games::qualifies(source.top_scores, finished) && source.top_scores[1].empty(),
            "draw profiles isolated");
    require(!games::add_top_score(source.top_scores, finished, "   "), "blank names rejected");
    require(!games::add_top_score(source.top_scores, finished, "A\nB"),
            "control characters rejected");
    source.games[0].draw();
    source.games[3].pass({0, 1, 2});
    source.games[3].play(source.games[3].computer_choice());
    require(games::save_cabinet(path, source), "save");
    games::Cabinet loaded;
    require(games::load_cabinet(path, loaded), "load");
    require(loaded.active == 2 && loaded.back == 3 && loaded.reduced, "settings roundtrip");
    require(loaded.player_name == source.player_name && loaded.result_recorded[2],
            "name and result latch roundtrip");
    require(loaded.top_scores[5].size() == 10 && loaded.top_scores[0][1].name == source.player_name,
            "named scores survive quit");
    for (int i = 0; i < 4; ++i) {
        require(loaded.games[i].invariant(), "saved conservation");
        require(loaded.games[i].state.seed == source.games[i].state.seed, "saved seed");
        for (int p = 0; p < 20; ++p) {
            const games::Pile& a = source.games[i].state.piles[p];
            const games::Pile& b = loaded.games[i].state.piles[p];
            require(a.size() == b.size(), "pile size");
            for (std::size_t c = 0; c < a.size(); ++c)
                require(a[c].id == b[c].id && a[c].up == b[c].up, "card order/visibility");
        }
    }
    {
        std::ofstream out(path, std::ios::app);
        out << "corrupt";
    }
    require(!games::load_cabinet(path, loaded), "corruption rejected");
    require(loaded.back == 3, "failed read preserves previous cabinet");
    {
        std::ofstream out(path);
        out << "RAINSTAR_GAMES 999 0\n";
    }
    require(!games::load_cabinet(path, loaded), "unknown version rejected");
    std::filesystem::remove_all(root);
    std::cout << "Passed: all-game saves, card ordering, settings, checksum, future-version "
                 "rejection and unchanged destination on failure.\n";
}
