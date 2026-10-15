#pragma once
// Rainstar Software's business card: a shell easter egg, not a game. It comes in two kinds.
//
// On the page: a small card lying beside the help book's dedication, still, casting its
// shadow on the paper and rising a little under the pointer. A click (or Enter) on it opens it.
//
// In the hand: the card opened over the whole window, alone on black. Dragging it turns it in
// three dimensions (held_card.hpp draws it, lit as it now faces the lamp), so the finish's
// dimples, the raised letters, the glint of its facets, the sheen of its varnish and the two
// plies of its cut edge move as it turns; let go, it spins down and settles face up or face
// down, wherever it was nearer. "Rainstar Software" turns to gold foil under the pointer and a
// click there opens the website; while the pointer is over the card, remarks of the sort made
// when such cards are compared (card_remarks.hpp) rise around it one at a time and fade.
// Escape, the close mark, or a click on the black puts it away.
//
// Both are printed on the card table's stock, with the finish the Settings screen chooses for
// the playing cards, at the same fineness in device pixels.
#include "card_remarks.hpp"
#include "cards/held_card.hpp"
#include "game_text.hpp"
#include "gui_forms/timer.hpp"
#include "presentation.hpp"

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace games {

class BusinessCard final : public gf::Control {
  public:
    enum class Kind { page, hand };
    BusinessCard(gf::StableId id, Kind kind);
    ~BusinessCard() override;
    static constexpr bool initialize_tree_after_construction = true;
    void initialize_control_tree();
    // On the page: where the card rests, its middle in this control's points, and its width.
    // `top_room`: how far above this control the card may still be seen (the book's heading
    // band, until the pages scroll the card up under it).
    void place(gf::Point centre, double width, double top_room);
    // Shown or hidden with what holds it.
    void set_showing(bool showing);
    std::function<void()> open;    // on the page: the card was clicked
    std::function<void()> close;   // in the hand: put away
    std::function<void(gf::PointerEvent&)> scroll;  // on the page: a wheel turned over the card

    void arrange(gf::Rect bounds) override;
    void on_paint(gf::Painter& painter, gf::Rect damage) override;
    void on_pointer(gf::PointerEvent& event) override;
    void on_key(gf::KeyEvent& event) override;
    void on_focus_cue_changed(bool visible) override;
    [[nodiscard]] bool hit_test_local(gf::Point local) const override;
    [[nodiscard]] gf::Insets visual_outsets() const noexcept override;

  protected:
    void on_attached_to_window() override;
    void on_detaching_from_window(gf::Window& window) noexcept override;

  private:
    // A remark around the card: where (points in this control), when it was said, how long it stays.
    struct Remark {
        std::string text;
        gf::Rect box;
        double born = 0;
        double life = 0;
    };
    // Lettering for the card, shaped by the text service at the faces' density.
    struct Letter {
        std::string text;
        TextImage image;
    };

    Kind kind_;
    // Where the card rests and how big it is.
    gf::Point centre_{};
    double width_ = 0;
    double top_room_ = 0;
    bool showing_ = false;
    bool focused_ = false;  // the keyboard is on it, and shows it
    std::shared_ptr<gf::Button> close_mark_;  // in the hand
    // The faces, made once the lettering is shaped, at `density_` texels a point.
    CardFace front_, back_;
    CardMaterial material_;
    double density_ = 0;
    double built_width_ = 0;
    gf::Rect company_{};  // "Rainstar Software" on the front, as fractions of the face
    std::unique_ptr<GameText> text_;
    bool text_failed_ = false;
    std::vector<Letter> letters_;
    Letter address_;
    // The picture: a square of `picture_points_` about the card's resting middle, kept as a
    // window image and patched where the card was and is.
    gf::ImageId image_{};
    double image_scale_ = 0;
    double picture_points_ = 0;
    int picture_pixels_ = 0;
    std::vector<std::byte> pixels_;
    std::vector<std::byte> patch_;
    PixelBox drawn_{};
    CardPose shown_{};
    bool stale_ = true;
    // How it is held.
    Turn turn_{};
    double spin_x_ = 0, spin_y_ = 0;  // radians a second about the screen's x and y axes
    double lift_ = 0;
    double foil_ = 0;
    bool hovering_ = false;
    bool over_company_ = false;
    gf::Point pointer_{};
    bool pressed_on_card_ = false;
    bool pressed_on_black_ = false;
    bool dragged_ = false;
    gf::Point pressed_{};
    gf::Point last_{};
    double last_time_ = 0;
    // The remarks.
    CardRemarks remarks_;
    std::vector<Remark> said_;
    double next_remark_ = 0;
    std::uint32_t place_seed_ = 0x51ed2701U;
    // The clock.
    std::unique_ptr<gf::Timer> timer_;
    std::chrono::steady_clock::time_point start_{};
    double now_ = 0;

    [[nodiscard]] bool in_hand() const;
    [[nodiscard]] double clock() const;
    [[nodiscard]] double wanted_density() const;
    [[nodiscard]] CardPose pose() const;
    [[nodiscard]] gf::Point picture_origin() const;
    [[nodiscard]] Turn rest(bool front) const;
    [[nodiscard]] bool card_hit(gf::Point local, bool& company) const;
    void resize(double width);
    void wake();
    void tick();
    void shape_lettering();
    void build_faces();
    void forget();
    void draw();
    void say();
    void visit();
    void put_away();
    void hover(gf::Point local);
    void unhover();
};

}  // namespace games
