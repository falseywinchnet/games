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
std::shared_ptr<const gf::Theme> games_theme(ButtonSkin skin = ButtonSkin::slate);
gf::Color game_accent(int game);
// smooth blends the partly covered pixels at span ends; tiled shapes pass false.
void paint_polygon(gf::Painter& painter, const std::vector<gf::Point>& points, gf::Color color,
                   bool smooth = true);
// The window's device scale; polygon fills step one device row at a time.
void set_polygon_scale(double device_scale);
void paint_dialog(gf::Painter& painter, gf::Rect bounds, const std::string& title,
                  gf::Color accent);
} // namespace games
