#include "grass_art.hpp"

#include "garden.hpp"
#include "lawn_art.hpp"

#include <algorithm>
#include <cmath>

namespace mm {
namespace {

constexpr double art_ppm = 96.0;

std::uint32_t lerp_pixel(std::uint32_t a, std::uint32_t b, std::uint32_t weight) {
    // weight 0..256, per channel including alpha.
    const std::uint32_t keep = 256U - weight;
    const std::uint32_t red_blue = (((a & 0x00FF00FFU) * keep + (b & 0x00FF00FFU) * weight) >> 8U) & 0x00FF00FFU;
    const std::uint32_t alpha_green = ((((a >> 8U) & 0x00FF00FFU) * keep + ((b >> 8U) & 0x00FF00FFU) * weight) >> 8U) & 0x00FF00FFU;
    return red_blue | (alpha_green << 8U);
}

std::uint32_t bilinear(const Layer& layer, int x0, int y0, int x1, int y1, double fx, double fy) {
    const std::size_t w = static_cast<std::size_t>(layer.width);
    const std::uint32_t wx = static_cast<std::uint32_t>(fx * 256.0);
    const std::uint32_t wy = static_cast<std::uint32_t>(fy * 256.0);
    const std::uint32_t top = lerp_pixel(layer.px[static_cast<std::size_t>(y0) * w + static_cast<std::size_t>(x0)],
                                         layer.px[static_cast<std::size_t>(y0) * w + static_cast<std::size_t>(x1)], wx);
    const std::uint32_t bottom = lerp_pixel(layer.px[static_cast<std::size_t>(y1) * w + static_cast<std::size_t>(x0)],
                                            layer.px[static_cast<std::size_t>(y1) * w + static_cast<std::size_t>(x1)], wx);
    return lerp_pixel(top, bottom, wy);
}

// What the grass must grow around, on the corners of the garden's fine cells: the beds and the lawn's own edge.
// Distances are spread outwards from them a cell at a time (straight steps and diagonal steps), which is close
// enough for a strip of bare earth a few centimetres wide.
LawnScene scene_of(const Garden& garden) {
    LawnScene scene{};
    scene.width = lawn_cells_x + 1;
    scene.height = lawn_cells_y + 1;
    const std::size_t w = static_cast<std::size_t>(scene.width);
    scene.distance.assign(w * static_cast<std::size_t>(scene.height), 1000.0F);
    for (int y = 0; y < scene.height; ++y) {
        for (int x = 0; x < scene.width; ++x) {
            bool stop = x == 0 || y == 0 || x == scene.width - 1 || y == scene.height - 1;
            for (int dy = -1; dy <= 0 && !stop; ++dy) {
                for (int dx = -1; dx <= 0 && !stop; ++dx) {
                    const int cx = std::clamp(x + dx, 0, lawn_cells_x - 1);
                    const int cy = std::clamp(y + dy, 0, lawn_cells_y - 1);
                    stop = garden.obstacles[static_cast<std::size_t>(cy) * static_cast<std::size_t>(lawn_cells_x) + static_cast<std::size_t>(cx)] != 0;
                    // grass grows on under things that only stand on legs
                    for (const Prop& prop : garden.props) {
                        if (stop && (prop.kind == PropKind::grill || prop.kind == PropKind::chair) && inside_prop(prop, (cx + 0.5) * lawn_cell, (cy + 0.5) * lawn_cell))
                            stop = false;
                    }
                }
            }
            if (stop)
                scene.distance[static_cast<std::size_t>(y) * w + static_cast<std::size_t>(x)] = 0.0F;
        }
    }
    const float straight = static_cast<float>(lawn_cell);
    const float diagonal = static_cast<float>(lawn_cell * 1.41421356);
    for (int pass = 0; pass < 2; ++pass) {
        const int step = pass == 0 ? 1 : -1;
        for (int row = 0; row < scene.height; ++row) {
            const int y = pass == 0 ? row : scene.height - 1 - row;
            for (int column = 0; column < scene.width; ++column) {
                const int x = pass == 0 ? column : scene.width - 1 - column;
                float best = scene.distance[static_cast<std::size_t>(y) * w + static_cast<std::size_t>(x)];
                const int px = x - step;
                const int py = y - step;
                if (px >= 0 && px < scene.width)
                    best = std::min(best, scene.distance[static_cast<std::size_t>(y) * w + static_cast<std::size_t>(px)] + straight);
                if (py >= 0 && py < scene.height) {
                    best = std::min(best, scene.distance[static_cast<std::size_t>(py) * w + static_cast<std::size_t>(x)] + straight);
                    if (px >= 0 && px < scene.width)
                        best = std::min(best, scene.distance[static_cast<std::size_t>(py) * w + static_cast<std::size_t>(px)] + diagonal);
                    const int qx = x + step;
                    if (qx >= 0 && qx < scene.width)
                        best = std::min(best, scene.distance[static_cast<std::size_t>(py) * w + static_cast<std::size_t>(qx)] + diagonal);
                }
                scene.distance[static_cast<std::size_t>(y) * w + static_cast<std::size_t>(x)] = best;
            }
        }
    }
    for (const Dandelion& flower : garden.dandelions)
        scene.flowers.push_back(LawnFlower{flower.clock ? 6 : 2, flower.x, flower.y});
    for (const Plant& plant : garden.plants) {
        const int tone = plant_tone(garden, plant);
        scene.plants.push_back(LawnPlant{static_cast<int>(plant.kind), tone, plant.x, plant.y, plant.seed});
    }
    for (const Bloom& bloom : garden.blooms)
        scene.flowers.push_back(LawnFlower{bloom.kind, bloom.x, bloom.y});
    for (const Mushroom& mushroom : garden.mushrooms)
        scene.flowers.push_back(LawnFlower{5, mushroom.x, mushroom.y});
    return scene;
}

bool cancelled(const std::atomic<bool>* cancel) {
    return cancel != nullptr && (*cancel).load();
}

} // namespace

std::uint32_t sample_clamped(const Layer& layer, double x, double y) {
    if (layer.empty())
        return 0;
    const double cx = std::clamp(x, 0.0, static_cast<double>(layer.width - 1));
    const double cy = std::clamp(y, 0.0, static_cast<double>(layer.height - 1));
    const double fx = std::floor(cx);
    const double fy = std::floor(cy);
    const int x0 = static_cast<int>(fx);
    const int y0 = static_cast<int>(fy);
    const int x1 = std::min(x0 + 1, layer.width - 1);
    const int y1 = std::min(y0 + 1, layer.height - 1);
    return bilinear(layer, x0, y0, x1, y1, cx - fx, cy - fy);
}

GrassArt make_grass_art(const Garden& garden, const std::atomic<bool>* cancel) {
    GrassArt art{};
    art.ppm = art_ppm;
    const LawnScene scene = scene_of(garden);
    art.tall = render_lawn(LawnLook::tall, scene, garden.seed, lawn_width, lawn_height, art_ppm);
    if (cancelled(cancel))
        return art;
    art.mown_dark = render_lawn(LawnLook::mown_dark, scene, garden.seed, lawn_width, lawn_height, art_ppm);
    if (cancelled(cancel))
        return art;
    art.mown_light = render_lawn(LawnLook::mown_light, scene, garden.seed, lawn_width, lawn_height, art_ppm);
    if (cancelled(cancel))
        return art;
    art.mown_quarter = render_lawn(LawnLook::mown_quarter, scene, garden.seed, lawn_width, lawn_height, art_ppm);
    if (cancelled(cancel))
        return art;
    art.mown_three_quarter = render_lawn(LawnLook::mown_three_quarter, scene, garden.seed, lawn_width, lawn_height, art_ppm);
    if (cancelled(cancel))
        return art;
    art.mulch = render_lawn(LawnLook::mulch, scene, garden.seed, lawn_width, lawn_height, art_ppm);
    if (cancelled(cancel))
        return art;
    art.beds = render_lawn(LawnLook::beds, scene, garden.seed, lawn_width, lawn_height, art_ppm);
    for (const Tree& tree : garden.trees) {
        if (cancelled(cancel))
            return art;
        art.crowns.push_back(render_tree(tree.kind, tree.crown, tree.seed, art_ppm));
    }
    return art;
}

} // namespace mm
