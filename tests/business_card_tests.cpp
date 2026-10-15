// The help book's business card: the held card draws where it is and nowhere else, its shadow
// falls beside it, the eye's ray finds the face that is turned toward it, and the remarks made
// about it are whole sentences, varied, with no slot left unfilled.
#include "card_remarks.hpp"
#include "cards/held_card.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

using namespace games;

namespace {

int failures = 0;
void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAILED: %s\n", what);
        ++failures;
    }
}

// A plain face: flat bone stock, no ink, no relief.
CardFace plain_face(int width, int height) {
    CardFaceMap map;
    map.width = width;
    map.height = height;
    map.texels.resize(static_cast<std::size_t>(width) * height);
    for (CardTexel& t : map.texels) {
        t.red = .8F;
        t.green = .77F;
        t.blue = .66F;
    }
    const CardFace face = card_face_levels(map);
    return face;
}
int alpha_at(const std::vector<std::byte>& pixels, int side, int x, int y) {
    const int a = std::to_integer<int>(pixels[(static_cast<std::size_t>(y) * side + x) * 4 + 3]);
    return a;
}

void held_card_draws_and_picks() {
    const CardFace front = plain_face(128, 73), back = plain_face(128, 73);
    check(front.levels.size() >= 3, "a face keeps its smaller levels");
    const CardMaterial material;
    const int side = 300;
    std::vector<std::byte> pixels(static_cast<std::size_t>(side) * side * 4, std::byte{0});
    CardPicture picture{pixels.data(), side, side, static_cast<std::size_t>(side) * 4, 1};
    CardPose pose;
    pose.width = 140;
    pose.height = 80;
    pose.centre_x = 150;
    pose.centre_y = 150;
    pose.lift = 12;
    pose.eye = 840;
    const PixelBox box = held_card_box(pose, picture);
    check(box.x0 > 0 && box.x1 < side && box.y0 > 0 && box.y1 < side, "the card and its shadow fit the picture");
    check(box.x0 <= 150 - 70 && box.x1 >= 150 + 70, "the box holds the card's width");
    // Mark the picture outside the box: drawing must leave it alone.
    pixels[0] = std::byte{7};
    draw_held_card(front, back, material, pose, picture, box);
    check(std::to_integer<int>(pixels[0]) == 7, "nothing is written outside the card's box");
    check(alpha_at(pixels, side, 150, 150) == 255, "the card covers its middle");
    // The lamp is above and to the left: the shadow falls below and to the right of the card,
    // partly transparent, and not above it.
    const int beside = alpha_at(pixels, side, 150 + 72, 150 + 44);
    check(beside > 10 && beside < 200, "the shadow falls below and right of the card");
    check(alpha_at(pixels, side, 150 - 74, 150 - 46) == 0, "no shadow above and left of the card");
    // The eye finds the front; turned over, the back.
    const CardHit middle = held_card_hit(pose, 150, 150);
    check(middle.hit && middle.front && std::abs(middle.u - .5) < .01 && std::abs(middle.v - .5) < .01,
          "the middle of the front is at its middle");
    const CardHit corner = held_card_hit(pose, 150 - 69, 150 - 39);
    check(corner.hit && corner.u < .02 && corner.v < .03, "the top-left of the front is its top-left");
    check(!held_card_hit(pose, 150, 150 - 60).hit, "the pointer above the card misses it");
    pose.turn = turn_about(0, 1, 0, 3.14159265358979);
    check(!front_toward_eye(pose.turn), "turned over, the card shows its back");
    const CardHit turned = held_card_hit(pose, 150 - 60, 150);
    check(turned.hit && !turned.front && turned.u < .1, "the back's left is the front's right");
    // Half turn, edge-on: only a sliver of the edge is there to hit.
    pose.turn = turn_about(0, 1, 0, 3.14159265358979 / 2);
    check(!held_card_hit(pose, 150 - 30, 150).hit, "edge-on, the card is a sliver");
    // Turns compose and part.
    const Turn a = turn_about(1, 0, 0, .3), b = turn_about(0, 1, 0, .4);
    check(std::abs(turn_apart(turn_then(a, b), turn_then(a, b))) < 1e-9, "a turn is no angle from itself");
    check(std::abs(turn_apart(Turn{}, a) - .3) < 1e-9, "a turn's angle is its angle");
    const Turn half = turn_toward(Turn{}, a, .5);
    check(std::abs(turn_apart(Turn{}, half) - .15) < 1e-9, "halfway there is half the angle");
}

void remarks_are_whole_and_varied() {
    CardRemarks remarks(12345U);
    std::set<std::string> said;
    std::string previous;
    for (int i = 0; i < 400; ++i) {
        const std::string remark = remarks.next();
        check(!remark.empty(), "a remark has words");
        check(remark.find('{') == std::string::npos && remark.find('}') == std::string::npos, "every slot is filled");
        check(remark.front() >= 'A' && remark.front() <= 'Z', "a remark begins with a capital");
        const char end = remark.back();
        check(end == '.' || end == '?' || end == '!', "a remark ends a sentence");
        check(remark != previous, "no remark twice running");
        previous = remark;
        said.insert(remark);
    }
    check(said.size() > 250, "the remarks vary");
    std::printf("%zu different remarks in 400; for instance:\n", said.size());
    CardRemarks sample(99U);
    for (int i = 0; i < 8; ++i)
        std::printf("  %s\n", sample.next().c_str());
}

}  // namespace

int main() {
    held_card_draws_and_picks();
    remarks_are_whole_and_varied();
    if (failures > 0)
        return EXIT_FAILURE;
    std::printf("Business card passed\n");
    return EXIT_SUCCESS;
}
