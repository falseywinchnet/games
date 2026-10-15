// Prints a generated table as the player would meet it, for reading the puzzles and the
// voices at design time.   table_demo [seed] [birds] [liars] [question 0/1] [tier]
#include "logic.hpp"
#include "perching.hpp"
#include "script.hpp"

#include <cstdio>
#include <cstdlib>

using namespace pt;

int main(int argc, char** argv) {
    const std::uint64_t seed = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 1;
    GenParams g;
    g.birds = argc > 2 ? std::atoi(argv[2]) : 7;
    g.liars = argc > 3 ? std::atoi(argv[3]) : 1;
    g.question = argc > 4 && std::atoi(argv[4]) != 0;
    g.tier = argc > 5 ? std::atoi(argv[5]) : 2;
    g.seed = seed;
    const Layout L = make_layout(seed);
    std::vector<Bird> flock(static_cast<size_t>(g.birds));
    std::vector<bool> red(static_cast<size_t>(g.birds));
    for (int i = 0; i < g.birds; ++i) red[static_cast<size_t>(i)] = flock[static_cast<size_t>(i)].red = i == 2;
    const std::vector<int> at = settle(L, flock, seed);
    const Scene s = scene_of(L, at, red);
    const Puzzle p = generate(g, s);
    const char* nm[] = {"Ada", "Bram", "Cleo", "Dot", "Ezra", "Fig", "Gus", "Hattie", "Iggy", "Juno"};
    std::vector<std::string> names(nm, nm + g.birds);
    std::vector<Manner> manners;
    std::vector<int> species;
    std::vector<bool> liars;
    for (int i = 0; i < g.birds; ++i) {
        manners.push_back(static_cast<Manner>(i % kManners));
        species.push_back(i == 2 ? 0 : (i * 3 + 1) % 10);
        liars.push_back(p.roles[static_cast<size_t>(i)] == Role::liar);
    }
    Script sc(seed);
    sc.cast(names, manners, species, flock, s, static_cast<int>(seed % 12));
    sc.set_liars(liars);
    std::printf("%s\n\n", sc.opener(p.liar_count, p.birds).c_str());
    for (int i = 0; i < p.birds; ++i) {
        const Seat& st = s.seats[static_cast<size_t>(i)];
        const char* role = p.roles[static_cast<size_t>(i)] == Role::liar ? "LIAR" : p.roles[static_cast<size_t>(i)] == Role::truthful ? "TRUE" : "chat";
        std::printf("%-6s [%s%s] on %s%s%s%s bar %d%s\n", names[static_cast<size_t>(i)].c_str(), role, i == p.culprit ? " CULPRIT" : "", stuff_name(st.stuff),
                    st.shade ? ", shade" : ", sun", st.high ? ", high" : "", st.low ? ", low" : "", st.bar, st.red ? ", red" : "");
        if (i == p.asker) std::printf("    (question bird) %s\n", sc.ask_me(i).c_str());
        for (const Formula& f : p.claims[static_cast<size_t>(i)]) std::printf("    \"%s\"\n", sc.claim(i, f).c_str());
        std::printf("    chatter: \"%s\"\n", sc.chatter(i, 3).c_str());
    }
    for (size_t k = 0; k < p.offers.size(); ++k) std::printf("  Q%zu%s %s\n", k, static_cast<int>(k) == p.good ? "*" : " ", sc.question(p.asker, p.offers[k]).c_str());
    std::printf("\nsuspects before asking: %zu; difficulty %d\n", suspects(p).size(), p.difficulty);
    return 0;
}
