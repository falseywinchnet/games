#pragma once
#include "gui_forms/basic_controls.hpp"
#include <memory>
namespace games {
namespace gf = gui_forms;
enum class ButtonSkin { slate, ivory, blue, mint };
class GameButton : public gf::Button {
  public:
    GameButton(gf::StableId id, std::string text);
    void set_skin(ButtonSkin skin);
    void on_paint(gf::Painter& painter, gf::Rect damage) override;

  private:
    ButtonSkin skin_ = ButtonSkin::ivory;
};
class GameTile final : public gf::Button {
  public:
    GameTile(gf::StableId id, int game);
    int game_index() const {
        return game_;
    }
    bool open_requested() const {
        return open_requested_;
    }
    void on_pointer(gf::PointerEvent& e) override;
    void on_key(gf::KeyEvent& e) override;
    void on_paint(gf::Painter& painter, gf::Rect damage) override;

  private:
    int game_ = 0;
    bool open_requested_ = false;
};
class LibrarySurface final : public gf::Control {
  public:
    explicit LibrarySurface(gf::StableId id);
    int selection = 0, category = 0;
    void arrange(gf::Rect bounds) override;
    void on_paint(gf::Painter& painter, gf::Rect damage) override;
};
std::shared_ptr<const gf::Theme> games_theme(ButtonSkin skin = ButtonSkin::slate);
gf::Color game_accent(int game);
const char* collection_title(int game);
void paint_polygon(gf::Painter& painter, const std::vector<gf::Point>& points, gf::Color color);
void paint_emblem(gf::Painter& painter, gf::Rect bounds, int game);
void paint_heading(gf::Painter& painter, gf::Rect bounds, const std::string& title, int game);
void paint_instrument(gf::Painter& painter, gf::Rect bounds, const std::string& label,
                      gf::Color accent);
void paint_dialog(gf::Painter& painter, gf::Rect bounds, const std::string& title,
                  gf::Color accent);
} // namespace games
