#pragma once
// The lair: a porthole onto space, dark walls with red light, and the
// villain's desk whose top is the glowing code console. Draws the scene and
// answers screen-space picking for the console.
#include "board.hpp"
#include "villain.hpp"
#include "platform/render.hpp"

#include <array>

namespace fp {

struct ConsoleState {
    Code draft{-1, -1, -1, -1};
    std::array<double, kPegs> pop{};     // a little bounce when a peg lands (decays)
    int hover_socket = -1, hover_palette = -1;
    int drag_color = -1;                 // a peg being dragged
    V3 drag_at{};                        // where it hangs over the console
    bool can_check = false;
    bool check_down = false;
    double press = 0;                    // 0..1 the red button travelling down (springs back)
    int turn = 1, turns = kTurns;        // the readout: the guess being made
    Score last;                          // pins for the latest judged guess
    double pins = 0;                     // 0..1 how many of those pins have lit (in order)
    double reboot = 0;                   // 0..1 a new game's scanline sweep across the console
    Code secret{};                       // shown rising from the desk when the game ends
    double reveal = 0;                   // 0..1
    double alarm = 0;                    // 0..1 red alert as the last turns approach
};

struct LairState {
    VillainPose villain;
    ConsoleState console;
    double doom = 0;   // 0..1 his plan succeeds and the lair falls apart around him
};

class Lair {
public:
    R3D r;
    static constexpr int kSamples = 2;  // samples per game pixel each way
    void resize(int w, int h);  // in game pixels
    void present(Canvas& out) const;  // the averaged picture, one game pixel each
    void render(const LairState& s, double t);

    // console geometry (world units, z up, the player looks toward +y)
    static constexpr double kDeskZ = 1.0, kDeskFront = -2.05, kDeskBack = .55, kDeskX = 3.0;
    static V3 socket(int i) { return {-.81 + .54 * i, -1.12, kDeskZ}; }
    static V3 palette(int c) { return {-1.75 + .7 * c, -1.72, kDeskZ}; }
    static V3 check_button() { return {1.45, -.72, kDeskZ}; }  // a big red push button, behind and right of the sockets
    static V3 readout() { return {-1.75, -.6, kDeskZ}; }

    void to_screen(V3 p, double& sx, double& sy) const;
    int pick_socket(double sx, double sy) const;
    int pick_palette(double sx, double sy) const;
    bool pick_check(double sx, double sy) const;
    bool pick_villain(double sx, double sy, const VillainPose& v) const;
    bool console_point(double sx, double sy, V3& out) const;  // the desk-top point under the cursor

private:
    void draw_room(const LairState& s, double t);
    void draw_desk(const LairState& s, double t);
    void draw_peg(int color, V3 base, double scale, double t, bool glow = false);
    void draw_collapse(const LairState& s, double t);  // debris, dust and cracks while it all comes down
};

}  // namespace fp
