#include "rocks.hpp"

#include <barrier>
#include <cstdio>
#include <thread>
#include <vector>

namespace {

void make_on_worker(std::barrier<>* ready, zc::Rock* result) {
    (*ready).arrive_and_wait();
    *result = zc::make_rock(7, 0);
    // Exercise both lazy subdivision levels on the same cold start.
    (*ready).arrive_and_wait();
    zc::ensure_near_mesh(*result);
}

bool same_mesh(const zc::RockMesh& a, const zc::RockMesh& b) {
    if (a.level != b.level || a.triangles.size() != b.triangles.size() || a.triangles.empty()) {
        return false;
    }
    for (size_t i = 0; i < a.triangles.size(); i += 1) {
        const zc::Vtx& x = a.triangles[i];
        const zc::Vtx& y = b.triangles[i];
        if (x.p.x != y.p.x || x.p.y != y.p.y || x.p.z != y.p.z ||
            x.n.x != y.n.x || x.n.y != y.n.y || x.n.z != y.n.z || x.s != y.s || x.t != y.t ||
            x.c.r != y.c.r || x.c.g != y.c.g || x.c.b != y.c.b || x.c.a != y.c.a) {
            return false;
        }
    }
    return true;
}

}  // namespace

int main() {
    constexpr int workers = 8;
    std::barrier ready(workers);
    std::vector<zc::Rock> rocks(workers);
    std::vector<std::thread> threads;
    for (int i = 0; i < workers; i += 1) {
        threads.emplace_back(make_on_worker, &ready, &rocks[static_cast<size_t>(i)]);
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    zc::Rock reference = zc::make_rock(7, 0);
    zc::ensure_near_mesh(reference);
    for (const zc::Rock& rock : rocks) {
        if (!same_mesh(rock.far_mesh, reference.far_mesh) || !same_mesh(rock.near_mesh, reference.near_mesh)) {
            std::fprintf(stderr, "Concurrent cold-start mesh differs from serial construction\n");
            return 1;
        }
    }
    std::puts("Eight concurrent cold-start rocks match serial far and near meshes");
    return 0;
}
