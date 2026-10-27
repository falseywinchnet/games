# Zen Construction — rigid-body physics engine specification

You are writing the physics engine for **Zen Construction**, a calm rock-stacking game. The player stacks irregular rocks on a sand bed using a toy crane. The crane holds each rock on four wires; the player lowers it gently onto the pile and presses Space to let go. The whole game lives or dies on this engine.

The engine must be **our own code**: no third-party physics, geometry or math libraries. Return it as described in "Deliverables". Everything here is binding unless it says "suggested". If something is genuinely ambiguous, choose the option that makes rocks behave more like real rocks at rest, and write your choice down in `phys/NOTES.md`.

## 1. What the engine must feel like

- **Rocks at rest stay at rest.** No jitter, creep, drift, slow rotation, "dancing" or buzzing, ever, including tall stacks (15 or more rocks) of irregular shapes with big mass ratios.
- **Nothing explodes.** No body gains energy from contact. Overlaps resolve gently, never with a pop.
- **Consequences are honest.** If a placement makes the stack unstable (centre of mass beyond the support, too steep a contact for the friction), the stack tips, slides or falls, promptly and plausibly. If it is stable, nothing moves.
- **Neither too sensitive nor too coarse.** A rock balanced with its centre of mass 2% inside its support edge stays; 2% outside, it topples.
- **Gentle hands.** A rock held by the crane can be lowered until it just touches. Its weight transfers gradually as the hook keeps descending. A held rock can't shove the pile harder than a small fraction of its own weight.

## 2. Scope

| | |
| --- | --- |
| Bodies | Up to 80 dynamic rigid bodies in the world at once (typically 5–25). |
| Shapes | Each body is a **compound of 1–4 convex hulls**, each with at most 48 vertices. |
| Statics | One infinite ground plane (the sand bed). Nothing else is static. |
| Constraints | One special "hold" constraint (the crane), §8. No other joints. |
| Units | Metres, kilograms, seconds, radians. **Z is up**; gravity is (0, 0, −9.81). |
| Scale | Rocks are 2–25 cm across; masses from 0.01 kg to 15 kg. Mass ratios between touching rocks of up to 500:1 must be stable. |
| Threads | None. Single-threaded, deterministic. |

## 3. Code rules

- C++20, standard library only. Namespace `zc::phys`. Everything in a directory `phys/`.
- **Doubles throughout** (positions, velocities, impulses). No floats in the simulation.
- Determinism: identical inputs on the same build must give bitwise-identical results. So:
  - iterate bodies and pairs in a fixed order (by id, then sub-shape index);
  - no `unordered_*` iteration in anything that affects results;
  - no threads;
  - no time-based randomness.
  - Compile with `-ffp-contract=off` (Clang/GCC) and `/fp:precise` (MSVC). The CMake target must set these.
- In the simulation step, use only `+ − × ÷` and `std::sqrt`, `std::abs`, `std::min`, `std::max`, `std::clamp`, `std::floor`. No `sin`/`cos`/`atan2`/`exp`/`pow` per step, because these vary across platforms. (Shape cooking may use anything.)
- No exceptions thrown from `step()`. No heap allocation in `step()` after the first few frames (reserve and reuse).
- No `M_PI` (define `constexpr double kPi`). No platform headers. Must compile warning-free with `-Wall -Wextra` on Clang and GCC, and `/W4` on MSVC.
- Style: snake_case functions and variables, PascalCase types, 4-space indent, comments only where the code isn't self-evident.

## 4. Public API (exact)

`phys/physics.hpp` must declare exactly this. You may add private members and helpers, and extra public *query* functions, but do not change these signatures or meanings.

```cpp
#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace zc::phys {

struct Vec3 { double x = 0, y = 0, z = 0; };      // provide + - * (scalar) dot cross length normalized
struct Quat { double w = 1, x = 0, y = 0, z = 0; }; // unit; provide multiply, conjugate, rotate(Vec3), from_axis_angle
struct Pose { Vec3 p; Quat q; };                   // p = the body's centre of mass in world space

using BodyId = std::int32_t;     // assigned 0,1,2,... and never reused within a World
constexpr BodyId kGround = -1;   // the ground plane, in contacts and raycasts

struct HullDesc { std::vector<Vec3> points; };     // any points; the engine takes their convex hull
struct ShapeDesc {
    std::vector<HullDesc> hulls;                   // 1..4 hulls, all in one shared local frame
    double density = 2600;                         // kg/m^3 (granite-ish)
    double friction = 0.75;                        // Coulomb coefficient, rock against rock
    double restitution = 0.05;
    double rolling_resistance = 0.002;             // metres (lever arm); see §7.6
};

struct Shape {                                     // "cooked": produced by cook(), shareable by many bodies
    std::vector<std::vector<Vec3>> hull_vertices;  // per hull, in the body frame (origin at the centre of mass)
    // (your private hull data: faces, planes, edges, adjacency)
    Vec3 com_offset;     // the centre of mass in the ShapeDesc's input frame; body frame = input frame − com_offset
    double mass = 0, volume = 0, radius = 0;       // radius: max distance of any vertex from the centre of mass
    std::array<double, 9> inertia{};               // body-frame inertia tensor about the centre of mass, row-major
    double friction = 0.75, restitution = 0.05, rolling_resistance = 0.002;
};
Shape cook(const ShapeDesc& desc);                 // quickhull + mass properties, §5

struct BodyState { Pose pose; Vec3 v; Vec3 w; bool asleep = false; };

struct Contact {                                   // one contact point from the last step
    BodyId a, b;                                   // a < b, except b = kGround for the ground (a is the body)
    Vec3 point;                                    // world, midway between the surfaces
    Vec3 normal;                                   // unit; the direction b pushes a (from b toward a)
    double separation;                             // metres; negative when overlapping
    double normal_impulse;                         // N·s accumulated over the last frame (sum over substeps)
};

struct HoldParams {
    double lin_hertz = 3.0, lin_zeta = 1.0;        // spring of the hook toward its target position
    double ang_hertz = 2.0, ang_zeta = 1.0;        // and toward its target orientation
    double max_lift = 1.5;                         // wire tension cap, in units of the body's weight m·g
    double max_lateral = 0.35;                     // horizontal force cap, in units of m·g
    double max_torque = 0.35;                      // torque cap, in units of m·g·radius
};
struct HoldState {
    double tension = 0;                            // wire force / (m·g) over the last frame: 1 hanging free, 0 slack
    Vec3 position_error;                           // target − actual centre of mass
};

struct WorldParams {
    Vec3 gravity{0, 0, -9.81};
    double frame_dt = 1.0 / 60.0;                  // one step() advances this much
    int substeps = 8;
    double contact_hertz = 30.0;                   // soft contact stiffness (capped at 0.25 × substep rate)
    double contact_damping = 10.0;                 // damping ratio of the soft contact
    double push_max_velocity = 0.5;                // m/s; the fastest overlap is ever pushed apart
    double linear_slop = 0.0005;                   // m; allowed resting overlap
    double speculative_distance = 0.002;           // m; base contact margin (plus motion, §6.4)
    double linear_damping = 0.02, angular_damping = 0.05;   // 1/s
    double sleep_linear = 0.004, sleep_angular = 0.02;      // m/s, rad/s
    double sleep_time = 0.5;                       // s below both thresholds before an island sleeps
    double ground_z = 0.0, ground_friction = 0.9;
    double restitution_threshold = 1.0;            // m/s; slower impacts never bounce
};

class World {
public:
    explicit World(const WorldParams& params = {});

    BodyId add_body(const Shape& shape, const Pose& pose, Vec3 v = {}, Vec3 w = {}, bool start_asleep = false);
    void remove_body(BodyId id);                   // also releases the hold if it held this body; wakes its contacts
    bool has(BodyId id) const;
    std::vector<BodyId> bodies() const;            // ascending ids
    BodyState state(BodyId id) const;
    void set_pose(BodyId id, const Pose& pose);    // teleport; zeroes velocity; wakes the body and its contacts
    void wake(BodyId id);                          // wakes the whole touching group (§9)

    // the crane, §8: at most one held body at a time
    void hold(BodyId id, const Pose& target, const HoldParams& params = {});
    void set_hold_target(const Pose& target);
    void release();
    std::optional<BodyId> held() const;
    HoldState hold_state() const;

    void step();                                   // advance by frame_dt

    std::vector<Contact> contacts() const;         // last step's contacts, deterministic order (a, b, sub-shapes, point)
    bool resting(BodyId id) const;                 // asleep, or below the sleep thresholds for ≥ 0.25 s
    std::optional<std::pair<BodyId, double>> raycast(Vec3 origin, Vec3 dir, double max_t) const;  // nearest hit (body or kGround), t along unit dir
    bool all_asleep() const;                       // ignoring a held body
    double kinetic_energy() const;
    std::uint64_t checksum() const;                // hash of the bit patterns of every pose and velocity, for determinism tests
    bool out_of_bounds(BodyId id) const;           // centre below z = −0.5 or more than 5 m from the origin horizontally

    const WorldParams& params() const;
};

}  // namespace zc::phys
```

## 5. Shape cooking

1. **Convex hull** of each `HullDesc` by quickhull.
   - Weld points closer than 0.1 mm, and merge coplanar faces (normals within 0.5°) into convex polygons.
   - Reject degenerate input (fewer than 4 non-coplanar points) with a clear assert in debug builds and a fallback tetrahedron in release.
   - If a hull would exceed 48 vertices, simplify it, keeping the volume within 2%.
2. **Mass properties** of the compound: volume, centre of mass and inertia tensor, exactly. Decompose each hull into tetrahedra from an interior point and use the closed-form tetrahedron integrals (e.g. Tonon 2004, or Eberly's "Polyhedral Mass Properties"). Overlapping hulls in one compound may be counted twice; that's acceptable (rock generation keeps overlaps small).
3. **Recentre**: move all hull vertices so the centre of mass is the origin of the body frame, and return `com_offset`.
4. Store per hull:
   - vertices and face polygons, with outward unit normals and plane offsets;
   - unique edges, with each edge's two adjacent faces (for the Gauss-map edge test);
   - a vertex → incident-faces adjacency (for the incident-face search).
5. `radius` = max vertex distance from the centre of mass.
6. The inverse inertia must be well defined: clamp each principal moment to at least 1e-9 kg·m².

## 6. Collision detection

Collision runs **once per frame**, at the start of `step()`. The solver then tracks separations through the substeps (§7.3).

### 6.1 Broadphase

Brute force over all body pairs, by ascending id, with world AABBs of each hull expanded by the pair margin (§6.4). That's fine for 80 bodies. Skip pairs where both bodies are asleep.

### 6.2 Hull vs hull: SAT with Gauss-map edge pruning

Follow Dirk Gregorius, "The Separating Axis Test between Convex Polyhedra" (GDC 2013):

1. Find the best face axis of A against B, and of B against A (max separation of the support point along each face normal).
2. Find the best edge-edge axis, testing only edge pairs whose Gauss-map arcs intersect (the edges build a face on the Minkowski difference).
3. If the max separation exceeds the pair margin, there is no contact.
4. Prefer a face axis unless the edge axis is better by more than `0.001 + 0.05 × |face separation|`. Also prefer A's face over B's under the same rule. The bias gives temporal coherence and stops manifold flicker.
5. **Face contact:**
   - take the reference face;
   - choose the incident face on the other hull (the most anti-parallel face among those touching the support vertex);
   - clip the incident polygon against the reference face's side planes (Sutherland–Hodgman);
   - keep points within the margin below or above the reference plane.
6. **Edge contact:** a single point, the midpoint of the closest points of the two edges.
7. **Reduce to at most 4 points:**
   - the deepest point;
   - the point farthest from it;
   - the point maximising the triangle area with those two;
   - the point maximising the quadrilateral area.

   This must be stable: the same input gives the same 4 points.

### 6.3 Hull vs ground

- Every vertex with `z − ground_z <` the pair margin is a candidate point, with normal (0, 0, 1) and separation `z − ground_z`.
- Reduce the candidates to 4 as in §6.2.

### 6.4 Margins (speculative contacts)

- Pair margin = `speculative_distance + (|v_a| + |w_a|·r_a + |v_b| + |w_b|·r_b) × frame_dt`, capped at 0.05 m.
- Contacts with positive separation inside the margin are **speculative**: they only stop approach (§7.4).
- This replaces continuous collision detection. A 1 cm-thick slab dropped from 0.5 m must not tunnel.

### 6.5 Manifold persistence (warm starting)

For each pair (body ids, hull indices) keep last frame's manifold. Match a new point to an old one when:

- their positions relative to body A are within 2 mm, **and**
- the normal changed by less than ~25° (dot > 0.9).

Matched points inherit the old normal and tangent impulses, rotated into the new tangent basis. Unmatched points start at zero.

## 7. Solver: substepped soft-step sequential impulses

Use Erin Catto's "soft step" scheme (Box2D v3, "Solver2D" blog posts, 2024), extended to 3D. Per `step()`:

```
collide()                                    // §6, once per frame
prepare contacts: anchors relative to each COM, normal/tangent effective masses, initial separation
h = frame_dt / substeps
for each substep:
    integrate velocities: v += h·g;  v *= 1/(1 + h·linear_damping);  w *= 1/(1 + h·angular_damping)
    apply hold forces (§8) as soft constraint impulses (solve after contacts, below)
    warm start contacts (apply stored impulses)
    solve contacts, use_bias = true          // §7.2
    solve hold, use_bias = true
    integrate positions: p += h·v;  q += ½·h·(0,w)·q, then normalise q
    relax: solve contacts and hold, use_bias = false
apply restitution (§7.5)
store impulses for warm starting; update sleep (§9)
```

Iteration order: contacts by (a, b, hull a, hull b, point index), then the hold. Gyroscopic torque is ignored (rocks rarely spin fast).

### 7.1 Soft-constraint coefficients (per substep h)

```
omega = 2π · min(contact_hertz, 0.25 / h)
zeta  = contact_damping
a1 = 2·zeta + h·omega;  a2 = h·omega·a1;  a3 = 1 / (1 + a2)
bias_rate = omega / a1;  mass_scale = a2·a3;  impulse_scale = a3
```

(2π is a constant; this is not a per-step trig call.)

### 7.2 Normal impulse for one contact point

- `s` = the current separation, with §7.3 for tracking.
- `vn` = the relative normal velocity at the point, (v_a + w_a × r_a − v_b − w_b × r_b) · n.

```
if s > 0:            bias = s / h                       // speculative: allow closing exactly the gap
else if use_bias:    bias = max(bias_rate · min(s + linear_slop, 0), −push_max_velocity); use mass_scale, impulse_scale
else:                bias = 0;  mass_scale = 1; impulse_scale = 0
d = −normal_mass · mass_scale · (vn + bias) − impulse_scale · accumulated
new = max(accumulated + d, 0);  apply (new − accumulated) along n;  accumulated = new
```

Note `min(s + linear_slop, 0)` in the push-out: overlaps up to the slop are allowed and never pushed (and never pulled together), which removes resting buzz. Deeper overlap pushes harder, up to `push_max_velocity`.

### 7.3 Separation tracking within a frame

- Store each contact's anchors in each body's local frame at preparation.
- Each substep: `s = s0 + dot((Δp_a + R_a·local_a − R_a0·local_a) − (Δp_b + R_b·local_b − R_b0·local_b), n)`, where Δp is the COM displacement since the frame started.
- Use the current rotations; recomputing world anchors is fine.
- Do not re-run collision per substep.

### 7.4 Friction

- Two tangent directions per point, in a stable basis from the normal.
- Solve tangents after the normal, as a 2D impulse clamped to the **friction cone**: `|λ_t| ≤ μ · λ_n`. Clamp the vector's magnitude, not each axis.
- μ = sqrt(μ_a·μ_b) for rock-rock; the ground's μ with the body's μ for rock-ground (the same geometric mean).
- No bias on friction. Friction runs in both the solve and the relax passes.

### 7.5 Restitution

- After all substeps, for points whose relative normal velocity at the frame's start was below −`restitution_threshold` and that carry normal impulse: apply e = max(e_a, e_b) toward `−e·vn_start`.
- In practice rocks almost never bounce.

### 7.6 Rolling resistance

For each body in contact, after the relax pass, apply an angular impulse opposing w:

- its magnitude is at most `rolling_resistance × (the sum of that body's contact normal impulses this substep)`;
- it never reverses w.

This stops rounded rocks rolling forever on sand, without making them sticky.

## 8. The hold constraint (the crane)

One body at a time. Inputs:

- the target pose (from the game, which moves it slowly);
- `m` = the body's mass, `g` = |gravity|, `r` = its radius.

It acts at the centre of mass (the wires are drawn by the game). The held body never sleeps.

**Linear part.** A soft 3-DOF spring toward the target position (frequency `lin_hertz`, damping `lin_zeta`, the same coefficient formulas as §7.1 but with these values). It also has a **feed-forward** that cancels gravity, so a free-hanging rock sits exactly on target with no droop. Split the impulse into:

- **Vertical (the wires):**
  - `λ_up = m·g·h + spring_z`.
  - The accumulated vertical impulse per substep is clamped to `[0, max_lift·m·g·h]`.
  - Wires can only pull, never push.
  - As the target goes below the point where the rock rests on something, tension falls smoothly to zero. With the default 3 Hz spring, full weight transfer takes about g/ω² ≈ 2.8 cm of further descent.
- **Horizontal:** the spring's (x, y) impulse, with its magnitude clamped to `max_lateral·m·g·h`.

**Angular part.** A soft spring toward the target orientation:

- error = the vector part of `q_target · conj(q)`, times 2, with the sign chosen so the angle is ≤ π;
- frequency `ang_hertz`, damping `ang_zeta`, using the world inverse inertia;
- angular impulse magnitude clamped to `max_torque·m·g·r·h`.

**Reporting.** `HoldState::tension` = the sum over the frame of the vertical impulse ÷ (m·g·frame_dt). It's 1 when hanging free and 0 when slack. The game draws taut or slack wires from it.

`release()` removes the constraint. The body keeps its current velocity, which should be near zero.

## 9. Sleeping and islands

- **Islands** are groups of bodies connected by contacts with positive normal impulse in the last frame. The ground does not connect islands.
- A body is a sleep candidate when |v| < `sleep_linear` and |w| < `sleep_angular`.
- An island sleeps when every body in it has been a candidate for `sleep_time`. It then zeroes all velocities and keeps its contacts' stored impulses.
- Waking:
  - an awake body (or the held body) touching a sleeping body (pair margin overlap) wakes that body's whole island;
  - `set_pose`, `remove_body`, `hold`, `release` and `wake` wake the affected islands;
  - removing a body wakes everything it touched.
- Bodies added with `start_asleep = true` (loading a saved stack) form islands from their first collision pass but stay asleep until woken.
- Sleeping must never hide a true consequence. Waking is conservative: when in doubt, wake.

## 10. Robustness guards

- If any body's pose or velocity becomes non-finite:
  - restore its previous frame's pose;
  - zero its velocity;
  - count it in a debug counter (tests assert the counter stays 0).
- Clamp |v| to 20 m/s and |w| to 50 rad/s. These are safety nets; tests must never hit them.
- Initial overlaps (e.g. a rock spawned inside another) are pushed apart at no more than `push_max_velocity`. No popping.

## 11. Acceptance tests (all must pass)

Provide `tests/phys_tests.cpp`. Each test prints its measured numbers and PASS/FAIL. Exit non-zero on any failure.

**Test shapes**, implemented in the test file, deterministic from a seed:

- `box(sx, sy, sz)`.
- `slab(l, w, t)`: a box.
- `rock(seed, kind)`: 32 points on a deformed ellipsoid (random axes per kind), each scaled by 1 + 0.15·noise, then the convex hull. Kinds:
  - **round**: axes 6–9 cm;
  - **flat**: 10–20 × 8–15 × 2–4 cm;
  - **jagged**: 3–6 cm, with 0.3 noise.
- `concave_rock(seed)`: a compound of 3 overlapping rocks.

| # | Test | Pass condition |
| --- | --- | --- |
| 1 | **Rest.** Each kind of rock dropped from 1 cm onto the ground, 50 seeds, simulated 600 s. | After 3 s: every body asleep; total drift over the remaining time < 0.05 mm, rotation < 0.005°. Penetration at rest < 1 mm. |
| 2 | **Tall stack.** 15 flat rocks stacked by a placement helper (each set on the previous with its COM over the support centre, dropped from 2 mm), 20 seeds. | Stands for 600 s; every rock asleep within 5 s of the last placement; max drift < 0.3 mm; KE after settling is exactly 0 (asleep) and never rises while asleep. |
| 3 | **Mass ratio.** A 10 kg slab resting on three 0.02 kg pebbles on the ground; and a 0.02 kg pebble on a 10 kg slab. | Both stable 120 s; pebble penetration < 1 mm; no jitter (the pebbles' max speed after 2 s < 1 mm/s). |
| 4 | **Overhang.** A 20 × 5 × 2 cm slab resting on a 10 cm cube, overhanging so its COM is 2% of its length inside the cube's edge; and 2% outside. | Inside: stays (rotation < 0.1° over 60 s). Outside: tips past 10° within 3 s. Repeat for 1% and 5%: 5% must behave like 2%; 1% may go either way. |
| 5 | **Incline.** A 10 cm box resting on the ground, with gravity tilted by θ (equivalent to an incline; set μ = 0.75 for both the box and the ground), tan θ = μ − 0.05 and μ + 0.05. Then the same with the box resting on a heavy (100 kg) flat slab on the ground, to test rock-on-rock friction, measuring the box relative to the slab. | Below: the box creeps < 0.5 mm in 30 s. Above: it slides > 10 cm in 3 s. |
| 6 | **Drop.** A round rock dropped from 0.3 m onto the ground and onto a resting flat rock. | Bounce < 5 mm; no body exceeds its pre-impact speed after contact; settles (asleep) within 3 s. A 1 cm slab dropped from 0.5 m must not tunnel the ground or another slab. |
| 7 | **Hold.** A rock held 30 cm above the ground; the target moved 10 cm sideways over 2 s; rotated 90° about z over 2 s. | At rest, position error < 0.2 mm and tension = 1 ± 0.01. While moving, no overshoot beyond 2 mm after stopping. |
| 8 | **Gentle set-down.** A stable 5-rock stack. A 2 kg rock is held over it and lowered at 2 cm/s until tension < 0.05, held there 5 s, then released. | Before release: no stack rock moves > 0.3 mm, and tension falls monotonically (within 0.02 noise) as it descends. After release: the stack and the new rock settle and sleep within 5 s; total motion of the old rocks < 0.5 mm. |
| 9 | **Shove cap.** The same, but the held rock is driven 5 cm sideways into the top stack rock. | The force applied never exceeds the caps; measured from the stack's impulses, it is ≤ 0.35·m·g ± 5%. |
| 10 | **Honest consequence.** A stable 6-rock stack; then a heavy flat rock (5× the mass of the top rock) is placed so its COM sits 3 cm beyond the top rock's support. | The stack topples (at least one stack rock moves > 2 cm) within 5 s. With the COM 3 cm inside the support instead, nothing moves > 0.5 mm. |
| 11 | **Wake.** A sleeping 8-rock stack; one rock added on top; a rock removed from the middle. | Adding wakes the island. Removing a middle rock wakes everything above it, and the upper rocks fall onto the lower ones. |
| 12 | **Load asleep.** A settled stack's poses saved (doubles), the world rebuilt with `start_asleep = true`, then one gentle rock added. | Nothing moves on load (0.0 drift while asleep). After the new rock lands and everything sleeps again, old rocks have moved < 0.3 mm. |
| 13 | **Concave.** Compound rocks stacked 5 high. | As test 2. |
| 14 | **Determinism.** Test 2's scenario run twice in the same process, and in two processes. | Identical `checksum()` every frame. |
| 15 | **Robustness.** A rock spawned 2 cm inside another; 30 rocks dropped in a heap from random heights up to 0.5 m. | No non-finite values, no speed clamps hit, and no body ever moves faster than free fall from its drop height (+5%). Overlaps resolve at ≤ push_max_velocity. Everything sleeps within 10 s. |
| 16 | **Performance.** 60 bodies in a heap plus a 15-rock stack, every body awake. | `step()` averages < 1.5 ms on an Apple M-series core, Release build (print the mean and max). All asleep: < 0.1 ms. |

## 12. Deliverables

Return a directory `phys/` containing:

- `physics.hpp`: the exact API of §4.
- Sources, split as you like (suggested: `math.hpp`, `hull.cpp`, `mass.cpp`, `collide.cpp`, `solver.cpp`, `world.cpp`).
- `tests/phys_tests.cpp`: §11, plus the test shapes.
- `tools/phys_bench.cpp`: the performance scene, printing per-step timings.
- `CMakeLists.txt` defining:
  - `zc_phys` (STATIC, the flags of §3);
  - `phys_tests` (registered with CTest);
  - `phys_bench`.

  It must build standalone (`cmake -S phys -B build && cmake --build build && ctest --test-dir build`) and be includable with `add_subdirectory(phys)`.
- `NOTES.md`: design notes, any choices you made where this spec left room, every measured number from the test run, and anything you found fragile.

Do not include a renderer, window or game code. The game side (rock generation, crane, camera, rendering, saving) is built around this API separately.
