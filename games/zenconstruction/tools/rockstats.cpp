// Prints a run's rocks: class, size, mass, friction, hull and mesh sizes, and the time to make them.
#include "rocks.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    const std::uint32_t seed = argc > 1 ? static_cast<std::uint32_t>(std::strtoul(argv[1], nullptr, 10)) : 1u;
    const std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    int counts[5] = {0, 0, 0, 0, 0};
    double total_triangles = 0;
    for (int i = 0; i < 50; i += 1) {
        const zc::Rock rock = zc::make_rock(seed, i);
        counts[static_cast<int>(rock.recipe.kind)] += 1;
        total_triangles += static_cast<double>(rock.far_mesh.triangles.size() / 3);
        if (i < 12) {
            std::printf("%2d %-10s %5.1f cm %6.3f kg  mu %.2f  hull %zu verts  lichen %.2f vein %d\n", i, zc::rock_class_name(rock.recipe.kind),
                        rock.diameter * 100, rock.shape.mass, rock.friction, rock.shape.hulls[0].vertices.size(), rock.recipe.lichen,
                        rock.recipe.vein ? 1 : 0);
        }
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("classes: disc %d cobble %d block %d slab %d shard %d; far-mesh triangles %.0f; %.0f ms for 50\n", counts[0], counts[1],
                counts[2], counts[3], counts[4], total_triangles, ms);
    return 0;
}
