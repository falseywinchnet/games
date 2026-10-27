#pragma once
// Davy Jones' locker: the captain's cabin of a ship long sunk, where the
// drowned play dice. A round barrel-top table under a swinging lantern; the
// crew seated round its far side, each with a leather cup and five dice; your
// cup and dice at the near edge; the Keeper's jar where lost dice go. Behind
// them, a great round stern window full of dark water, and in it the Keeper
// of the locker himself: an enormous anglerfish whose lure glows when he
// speaks. Seaweed sways, bubbles rise, light falls in shafts from above.
#include "crew.hpp"

#include <vector>

namespace ld {

struct DieShow {
    int value = 1;
    double glow = 0;     // 0..1 counted at a reveal
    double hop = 0;      // 0..1 a little jump as it's counted
    double fade = 0;     // 0..1 not counted: dims
};
struct CupShow {
    double lift = 0;     // 0..1 off the dice (a reveal, or peeking at your own)
    double shake = 0;    // 0..1 rattling
};
struct Sink { int seat; double t; };   // a lost die on its way to the jar, t 0..1

struct CabinState {
    std::vector<CrewPose> crew;               // seats 1..n (index 0 unused: you)
    std::vector<CupShow> cups;                // per seat, you at 0
    std::vector<std::vector<DieShow>> dice;   // per seat
    std::vector<int> hidden;                  // per seat: dice under the cup (count shown) when not lifted
    int jar = 0;                              // dice in the Keeper's jar
    std::vector<Sink> sinking;
    double keeper = 0;                        // 0..1 the Keeper speaking: the lure brightens
    double tension = 0;                       // 0..1 the room darkens and closes in
    std::vector<std::pair<V3, double>> puffs; // bubbles let slip (from, age seconds)
};

class Cabin {
public:
    R3D r;
    void resize(int w, int h, int bottom_panel, int top_band = 0);  // the table is framed between the top band and the panel
    void seat(int opponents, CabinState& s);  // places the crew round the table, the cups and dice
    void render(const CabinState& s, double t);
    void to_screen(V3 p, double& sx, double& sy) const;
    int pick(double sx, double sy, const CabinState& s) const;  // which seat is under the point (-1 none)
    V3 cup_pos(int seat) const;
    V3 dice_pos(int seat, int i, int n) const;
    V3 jar_pos() const { return {-.72, -.08, kTop}; }
    static constexpr double kTop = .8;

private:
    int panel_ = 0, w_ = 0, h_ = 0, n_ = 2;
    void draw_room(const CabinState& s, double t);
    void draw_table(const CabinState& s, double t);
};

void draw_die(R3D& r, V3 base, double size, int value, double spin, Col body, Col pip, double glow);

}  // namespace ld
