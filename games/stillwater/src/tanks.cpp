#include "tanks.hpp"

#include "treasure.hpp"

namespace sw {
namespace {

using ambient::Vec3;

TankStyle planted_style() {
    // The original Riverscape colours (TankStyle's defaults), with its caustics.
    TankStyle style{};
    style.caustics.seed = 1;
    return style;
}

TankStyle reef_style() {
    TankStyle style{};
    style.hemisphere_low = {0.02F, 0.035F, 0.05F};
    style.hemisphere_high = {0.09F, 0.16F, 0.23F};
    style.fill = {0.05F, 0.09F, 0.13F};
    style.sun = {1.62F, 1.62F, 1.52F};
    style.caustic = {0.92F, 1.0F, 1.0F};
    style.caustic_share = 0.55F;
    style.absorption = {0.045F, 0.012F, 0.004F};
    style.water = {0.006F, 0.05F, 0.11F};
    style.fog_density = 0.0009F;
    style.tank_absorption = {0.045F, 0.012F, 0.006F};
    style.sand_tint = {1.25F, 1.2F, 1.1F};
    style.rock_tint = {1.25F, 0.95F, 1.05F};
    style.moss = 0.35F;
    style.bubble = {0.07F, 0.16F, 0.22F};
    style.bubble_rim = {0.42F, 0.6F, 0.72F};
    style.caustics.seed = 7;
    style.caustics.tile = 3.0F;
    // Blue tang: royal blue with a black sweep and a yellow tail.
    FishColors& tang = style.fish[0];
    tang.back = {0.004F, 0.008F, 0.05F};
    tang.upper = {0.01F, 0.035F, 0.26F};
    tang.flank = {0.02F, 0.11F, 0.62F};
    tang.belly = {0.05F, 0.22F, 0.66F};
    tang.sheen = {0.004F, 0.004F, 0.012F};
    tang.sheen_amount = 0.9F;
    tang.warm = {0.85F, 0.62F, 0.02F};
    tang.warm_amount = 0.95F;
    tang.cheek_dark = {0.01F, 0.03F, 0.2F};
    tang.cheek_light = {0.04F, 0.18F, 0.6F};
    tang.snout = {0.01F, 0.04F, 0.25F};
    tang.fin_root = {0.02F, 0.05F, 0.35F};
    tang.fin_tip = {0.85F, 0.62F, 0.02F};
    tang.reflect = 0.15F;
    // Clownfish: orange with three white bands edged in black.
    FishColors& clown = style.fish[1];
    clown.back = {0.55F, 0.10F, 0.0F};
    clown.upper = {0.72F, 0.17F, 0.0F};
    clown.flank = {0.85F, 0.24F, 0.01F};
    clown.belly = {0.9F, 0.33F, 0.02F};
    clown.sheen_amount = 0;
    clown.warm_amount = 0;
    clown.cheek_dark = {0.7F, 0.17F, 0.0F};
    clown.cheek_light = {0.85F, 0.26F, 0.01F};
    clown.snout = {0.8F, 0.22F, 0.0F};
    clown.bands = 3;
    clown.band = {0.86F, 0.86F, 0.84F};
    clown.band_edge = {0.01F, 0.01F, 0.01F};
    clown.fin_root = {0.8F, 0.2F, 0.0F};
    clown.fin_tip = {0.03F, 0.02F, 0.02F};
    clown.reflect = 0.1F;
    // Yellow tang: lemon yellow all over.
    FishColors& yellow = style.fish[2];
    yellow.back = {0.62F, 0.40F, 0.0F};
    yellow.upper = {0.78F, 0.55F, 0.0F};
    yellow.flank = {0.86F, 0.64F, 0.01F};
    yellow.belly = {0.9F, 0.72F, 0.06F};
    yellow.sheen_amount = 0;
    yellow.warm_amount = 0;
    yellow.cheek_dark = {0.75F, 0.52F, 0.0F};
    yellow.cheek_light = {0.88F, 0.68F, 0.04F};
    yellow.snout = {0.8F, 0.6F, 0.1F};
    yellow.fin_root = {0.8F, 0.58F, 0.0F};
    yellow.fin_tip = {0.9F, 0.7F, 0.05F};
    yellow.reflect = 0.12F;
    return style;
}

TankStyle pool_style() {
    TankStyle style{};
    style.hemisphere_low = {0.022F, 0.018F, 0.009F};
    style.hemisphere_high = {0.065F, 0.065F, 0.032F};
    style.fill = {0.035F, 0.035F, 0.022F};
    style.sun = {1.15F, 0.98F, 0.72F};
    style.caustic = {1.0F, 0.95F, 0.72F};
    style.caustic_share = 0.38F;
    style.absorption = {0.025F, 0.045F, 0.095F};
    style.water = {0.014F, 0.012F, 0.004F};
    style.fog_start = 12;
    style.fog_density = 0.0024F;
    style.tank_absorption = {0.03F, 0.05F, 0.095F};
    style.rock_tint = {0.9F, 0.85F, 0.75F};
    style.bubble = {0.1F, 0.09F, 0.05F};
    style.bubble_rim = {0.5F, 0.45F, 0.3F};
    style.caustics.seed = 3;
    style.caustics.floor_gain = 0.5F;
    style.caustics.period = 24;
    // Minnows: olive backs, brassy flanks, a dark stripe along the side.
    FishColors& minnow = style.fish[0];
    minnow.back = {0.02F, 0.025F, 0.01F};
    minnow.upper = {0.08F, 0.08F, 0.03F};
    minnow.flank = {0.24F, 0.23F, 0.15F};
    minnow.belly = {0.42F, 0.40F, 0.31F};
    minnow.sheen = {0.02F, 0.02F, 0.015F};
    minnow.sheen_amount = 0.8F;
    minnow.warm = {0.5F, 0.3F, 0.05F};
    minnow.warm_amount = 0.3F;
    minnow.cheek_dark = {0.05F, 0.05F, 0.02F};
    minnow.cheek_light = {0.4F, 0.37F, 0.25F};
    minnow.fin_root = {0.35F, 0.30F, 0.15F};
    minnow.fin_tip = {0.40F, 0.30F, 0.12F};
    minnow.reflect = 0.35F;
    // Rudd: bronze and silver, with red fins.
    FishColors& rudd = style.fish[1];
    rudd.back = {0.05F, 0.06F, 0.03F};
    rudd.upper = {0.15F, 0.15F, 0.06F};
    rudd.flank = {0.32F, 0.27F, 0.14F};
    rudd.belly = {0.45F, 0.41F, 0.33F};
    rudd.sheen = {0.7F, 0.5F, 0.15F};
    rudd.sheen_amount = 0.4F;
    rudd.warm = {0.6F, 0.1F, 0.02F};
    rudd.warm_amount = 0.3F;
    rudd.fin_root = {0.6F, 0.12F, 0.03F};
    rudd.fin_tip = {0.75F, 0.06F, 0.02F};
    rudd.reflect = 0.5F;
    // Darters: sandy brown with dark saddles, keeping to the bottom.
    FishColors& darter = style.fish[2];
    darter.back = {0.06F, 0.05F, 0.03F};
    darter.upper = {0.15F, 0.12F, 0.07F};
    darter.flank = {0.3F, 0.25F, 0.15F};
    darter.belly = {0.5F, 0.45F, 0.35F};
    darter.sheen_amount = 0;
    darter.warm_amount = 0;
    darter.bands = 5;
    darter.band = {0.05F, 0.04F, 0.025F};
    darter.band_edge = {0.05F, 0.04F, 0.025F};
    darter.fin_root = {0.3F, 0.25F, 0.15F};
    darter.fin_tip = {0.25F, 0.2F, 0.12F};
    darter.reflect = 0.2F;
    return style;
}

} // namespace

const TankStyle& tank_style(Tank tank) {
    static const TankStyle planted = planted_style();
    static const TankStyle reef = reef_style();
    static const TankStyle pool = pool_style();
    if (tank == Tank::reef)
        return reef;
    if (tank == Tank::pool)
        return pool;
    return planted;
}

Tank tank_for_archive(std::string_view path) {
    if (path.find("reef") != std::string_view::npos)
        return Tank::reef;
    if (path.find("pool") != std::string_view::npos)
        return Tank::pool;
    return Tank::planted;
}

std::unique_ptr<ambient::Look> make_tank_look(Tank tank, const ambient::SceneData& scene) {
    std::unique_ptr<ambient::Look> look = std::make_unique<RiverscapeLook>(scene, tank_style(tank));
    return look;
}

void dress_tank(Tank tank, ambient::SceneData& scene) {
    // The chest belongs on the planted tank's sand and on the reef; the river pool
    // has its own sunken branch instead.
    if (tank != Tank::pool)
        add_treasure(scene);
}

} // namespace sw
