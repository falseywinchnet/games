#include "grass_art.hpp"

#include "garden.hpp"

#include <algorithm>
#include <cmath>

namespace mm {
namespace {

constexpr double art_ppm = 96.0;

// What the grass must grow around, on the corners of the garden's fine cells: the beds and the lawn's own edge.
// Distances are spread outwards from them a cell at a time (straight steps and diagonal steps), which is close
// enough for a strip of bare earth a few centimetres wide.
grass::LawnScene scene_of(const Garden& garden) {
    grass::LawnScene scene{};
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
        scene.flowers.push_back(grass::LawnFlower{flower.clock ? 6 : 2, flower.x, flower.y});
    for (const Plant& plant : garden.plants) {
        const int tone = plant_tone(garden, plant);
        scene.plants.push_back(grass::LawnPlant{static_cast<int>(plant.kind), tone, plant.x, plant.y, plant.seed});
    }
    for (const Bloom& bloom : garden.blooms)
        scene.flowers.push_back(grass::LawnFlower{bloom.kind, bloom.x, bloom.y});
    for (const Mushroom& mushroom : garden.mushrooms)
        scene.flowers.push_back(grass::LawnFlower{5, mushroom.x, mushroom.y});
    return scene;
}

bool cancelled(const std::atomic<bool>* cancel) {
    return cancel != nullptr && (*cancel).load();
}

} // namespace

GrassArt make_grass_art(const Garden& garden, const std::atomic<bool>* cancel) {
    GrassArt art{};
    art.ppm = art_ppm;
    const grass::LawnScene scene = scene_of(garden);
    art.tall = grass::render_lawn(grass::LawnLook::tall, scene, garden.seed, lawn_width, lawn_height, art_ppm);
    if (cancelled(cancel))
        return art;
    art.mown_dark = grass::render_lawn(grass::LawnLook::mown_dark, scene, garden.seed, lawn_width, lawn_height, art_ppm);
    if (cancelled(cancel))
        return art;
    art.mown_light = grass::render_lawn(grass::LawnLook::mown_light, scene, garden.seed, lawn_width, lawn_height, art_ppm);
    if (cancelled(cancel))
        return art;
    art.mown_quarter = grass::render_lawn(grass::LawnLook::mown_quarter, scene, garden.seed, lawn_width, lawn_height, art_ppm);
    if (cancelled(cancel))
        return art;
    art.mown_three_quarter = grass::render_lawn(grass::LawnLook::mown_three_quarter, scene, garden.seed, lawn_width, lawn_height, art_ppm);
    if (cancelled(cancel))
        return art;
    art.mulch = grass::render_lawn(grass::LawnLook::mulch, scene, garden.seed, lawn_width, lawn_height, art_ppm);
    if (cancelled(cancel))
        return art;
    art.beds = grass::render_lawn(grass::LawnLook::beds, scene, garden.seed, lawn_width, lawn_height, art_ppm);
    for (const Tree& tree : garden.trees) {
        if (cancelled(cancel))
            return art;
        art.crowns.push_back(grass::render_tree(tree.kind, tree.crown, tree.seed, art_ppm));
    }
    return art;
}

} // namespace mm
