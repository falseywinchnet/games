// Rules and patience-ladder tests for Switchbox.
#include "mole.hpp"
#include "puzzle.hpp"

#include <cstdio>
#include <cstdlib>
#include <set>

using namespace sbx;

static int g_fail = 0;
#define CHECK(c)                                                              \
    do {                                                                      \
        if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++g_fail; } \
    } while (0)

static int g_rot = 0;  // vary the wrong switch: repeating one is not a new try
static int wrong_first(const Puzzle& p) { return (p.first() + 1 + (g_rot++ % (kSwitches - 1))) % kSwitches; }
static int wrong_at(const Puzzle& p) {  // any switch that is not the next one
    return (p.combination()[static_cast<size_t>(p.progress())] + 1) % kSwitches;
}
static Reaction all_wrong(Puzzle& p) { return p.flip(wrong_first(p)).reaction; }
static Reaction partial(Puzzle& p, int depth) {
    for (int i = 0; i < depth; ++i) p.flip(p.combination()[static_cast<size_t>(i)]);
    int w = wrong_at(p);
    return p.flip(w).reaction;
}

static void repeats_are_not_tries() {
    Puzzle p(23);
    const int w = (p.first() + 1) % kSwitches;
    for (int i = 0; i < 12; ++i) CHECK(p.flip(w).reaction == Reaction::none);  // the same wrong switch, over and over
    CHECK(p.steps() == 12);
    CHECK(all_wrong(p) == Reaction::none);  // a different one counts: the first new try
}

static void permutations() {
    std::set<std::array<int, kSwitches>> seen;
    Puzzle p(42);
    for (int i = 0; i < 200; ++i) {
        std::set<int> s(p.combination().begin(), p.combination().end());
        CHECK(s.size() == kSwitches && *s.begin() == 0 && *s.rbegin() == kSwitches - 1);
        seen.insert(p.combination());
        p.new_combination();
    }
    CHECK(seen.size() > 150);  // varied
}

static void solve_and_lamps() {
    Puzzle p(7);
    for (int i = 0; i < kSwitches; ++i) {
        const int sw = p.combination()[static_cast<size_t>(i)];
        CHECK(!p.lamp(sw));
        FlipResult r = p.flip(sw);
        CHECK(r.accepted && r.correct && r.lit == i + 1 && p.lamp(sw));
        CHECK(r.solved == (i == kSwitches - 1));
    }
    CHECK(p.solved() && p.steps() == kSwitches);
    CHECK(!p.flip(0).accepted);  // nothing more until a new combination
}

static void wrong_clears() {
    Puzzle p(9);
    p.flip(p.combination()[0]);
    p.flip(p.combination()[1]);
    const int lit0 = p.combination()[0];
    FlipResult r = p.flip(p.combination()[0]);  // already lit: not next
    CHECK(r.accepted && !r.correct && r.depth == 2 && r.lit == 0 && !p.lamp(lit0));
    CHECK(p.steps() == 3);
}

static void hint_after_four() {
    Puzzle p(11);
    CHECK(all_wrong(p) == Reaction::none);
    CHECK(all_wrong(p) == Reaction::none);
    CHECK(all_wrong(p) == Reaction::none);
    CHECK(all_wrong(p) == Reaction::hint_first);
    CHECK(all_wrong(p) == Reaction::none);
    CHECK(all_wrong(p) == Reaction::none);
    CHECK(all_wrong(p) == Reaction::scold);
    CHECK(all_wrong(p) == Reaction::none);
    CHECK(all_wrong(p) == Reaction::none);
    CHECK(all_wrong(p) == Reaction::rip_out);
    CHECK(p.ripped());
    for (int s = 0; s < kSwitches; ++s) CHECK(p.present(s) == (s == p.first()));
    CHECK(!p.flip(wrong_first(p)).accepted);  // missing switches cannot be flipped
    FlipResult r = p.flip(p.first());
    CHECK(r.correct && r.reaction == Reaction::restore && !p.ripped());
}

static void progress_resets_lost_track() {
    Puzzle p(13);
    all_wrong(p); all_wrong(p); all_wrong(p);
    CHECK(partial(p, 2) == Reaction::none);  // new best: clears the streak
    CHECK(all_wrong(p) == Reaction::none);
    CHECK(all_wrong(p) == Reaction::none);
    CHECK(all_wrong(p) == Reaction::forgot);  // three after progress
    CHECK(all_wrong(p) == Reaction::none);
    CHECK(partial(p, 3) == Reaction::none);   // getting more right: back to calm
    CHECK(all_wrong(p) == Reaction::none);
    CHECK(all_wrong(p) == Reaction::none);
    CHECK(all_wrong(p) == Reaction::forgot);
}

static void stuck_track() {
    Puzzle p(17);
    CHECK(partial(p, 2) == Reaction::none);  // best 2
    CHECK(partial(p, 1) == Reaction::none);
    CHECK(partial(p, 2) == Reaction::none);
    CHECK(partial(p, 2) == Reaction::none);
    CHECK(partial(p, 1) == Reaction::stuck_scold);
    for (int i = 0; i < 3; ++i) CHECK(partial(p, 2) == Reaction::none);
    CHECK(partial(p, 2) == Reaction::stuck_grumble);
    CHECK(partial(p, 3) == Reaction::none);  // a new best is never scolded
}

static void restore_roundtrip() {
    Puzzle p(19);
    partial(p, 2);
    all_wrong(p);
    Puzzle q(1);
    CHECK(q.restore(p.state()));
    CHECK(q.combination() == p.combination() && q.steps() == p.steps() && q.progress() == p.progress());
    PuzzleState bad = p.state();
    bad.combo[0] = bad.combo[1];
    CHECK(!q.restore(bad));
    bad = p.state();
    bad.progress = kSwitches;
    CHECK(!q.restore(bad));
}

// A perfect player (whacks every switch the frame it appears, 30 fps) can
// reach the special message; a sleepy one (half the pops) cannot.
static void mole_reachable() {
    for (int pass = 0; pass < 2; ++pass) {
        Mole m;
        m.start(99);
        int frame = 0;
        while (m.active()) {
            m.update(1.0 / 30);
            for (int i = 0; i < kSwitches; ++i)
                if (m.up(i) && (pass == 0 || (frame / 15) % 2 == 0)) m.hit(i);
            ++frame;
        }
        if (pass == 0) CHECK(m.hits() >= Mole::kGreat + 10);
        else CHECK(m.hits() < Mole::kGreat + 25);
        CHECK(m.finished() || !m.active());
    }
    Mole m;
    m.start(1);
    m.update(.5);
    CHECK(!m.hit(0));  // nothing counts during the ready pause
}

int main() {
    mole_reachable();
    permutations();
    repeats_are_not_tries();
    solve_and_lamps();
    wrong_clears();
    hint_after_four();
    progress_resets_lost_track();
    stuck_track();
    restore_roundtrip();
    if (g_fail) { std::printf("%d failures\n", g_fail); return 1; }
    std::printf("puzzle tests passed\n");
    return 0;
}
