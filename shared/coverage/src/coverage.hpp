#pragma once
// Coverage: a small autonomous-mowing (or vacuuming) brain. It is given nothing
// but the size of its field: the fence and everything inside it are discovered by
// a simulated forward scanner and, failing that, by the bumper. Everything the
// planner wants is recomputed from (belief, cut, pose), so it carries on sensibly
// from wherever it is put down.
//
//   sensing   a fan of rays ahead of the machine, reaching furthest straight
//             ahead like headlights, marching through the true obstacle grid;
//             free and solid cells are written to the belief map.
//   learning  a support field (distance to anything known solid, and how much
//             structure or doubt there is nearby) seeds germs: sparse over open
//             lawn, dense where the ground is intricate. Each germ owns the
//             ground geodesically nearest to it, and mown ground keeps its owner.
//   the menu  laps round the fence and round every bed (contours of the support
//             field), stripes across each owner's ground along its sweep axis,
//             and, when those are done, the tufts left standing. Each entry is
//             a line with grass still under it.
//   managing  the manager holds one tape: the planned way to a line, then the
//             line. It drives that to its end and only then chooses again,
//             looking a couple of jobs ahead when it does.
//   driving   tractor steering with a turning circle. Every move between lines
//             is drawn before it is driven (one forward curve if one fits, else
//             a planned turn with one or two changes of gear) and checked
//             against everything known. Driving is nothing but following.
//
// The planner works on a 0.1 m grid and records the cut on a 0.05 m grid.
// Everything is deterministic: positions are doubles, time is passed in, there is
// no randomness. Unknown space is assumed free and replanned when seen otherwise.
#include <cstdint>
#include <map>
#include <vector>

namespace coverage {

struct Pose {
    double x{};
    double y{};
    double heading{};  // radians; 0 points along +x, pi/2 along +y
};

struct Machine {
    double clearance{0.55};    // what a bump teaches: solid this close is marked, metres
    double deck_width{1.0};    // cutting width across the heading
    double deck_length{0.7};   // cutting length along the heading
    double lane_pitch{0.9};    // distance between sweep lanes
    double speed{1.0};         // metres per second when mowing straight
    double travel_speed{1.4};  // kept for callers; every move is driven at mowing speed
    double turn_rate{1.8};     // kept for callers; turning is bound by the turning circle
    double lidar_range{1.8};   // the scanner's reach at the edges of its fan
    double lidar_field{2.1};   // full field of view, radians
    int lidar_rays{61};
    double lidar_reach{4.6};      // the scanner's reach straight ahead
    double turning_radius{0.45};  // the tightest circle it can steer
    double body_radius{0.52};     // collision radius about the centre, metres
};

struct FieldSetup {
    double width{16};   // metres, x
    double height{10};  // metres, y
    double cell{0.05};  // resolution of the obstacles handed in, metres
    double pose_cell{0.15};  // kept for callers; unused
};

enum class Belief : std::uint8_t { unknown, free, occupied };
enum class Phase : std::uint8_t { sweeping, cleaning, goal, manual, finished };

struct Step {
    double speed{};     // metres per second achieved this step
    double turning{};   // radians per second
    bool bumped{};      // the bumper met an obstacle the scanner had not reported
    bool goal_reached{};
    int newly_cut{};    // fine cells cut this step
};

class Mower final {
  public:
    // `obstacles`: the true field at `field.cell` resolution, row-major, 1 where
    // blocked, without any fence: the two outermost planner cells are fenced here.
    // The machine starts at `start`, which must be clear.
    Mower(FieldSetup field, Machine machine, std::vector<std::uint8_t> obstacles, Pose start);

    // Senses, plans and drives for `seconds` (call at 10 to 60 Hz).
    Step update(double seconds);
    // Someone has taken the controls: roll toward a point (field coordinates) as
    // tightly as the turning circle allows. Collisions are still prevented and
    // coverage continues to be recorded.
    Step drive_toward(double x, double y, double seconds);
    // Hands control back to the manager, which chooses afresh from where it is.
    // Ground the mower's own mind keeps off but a hand on the wheel may cross (flower
    // beds): `solid` marks, on the field's cells, the obstacles that stop it even then.
    void set_solid(const std::vector<std::uint8_t>& solid);
    void resume();
    // Go and put the deck over this point next (a found object), then carry on.
    void set_goal(double x, double y);
    void clear_goal();

    [[nodiscard]] const Pose& pose() const {
        return pose_;
    }
    [[nodiscard]] Phase phase() const;
    [[nodiscard]] bool finished() const {
        return finished_ && !held_;
    }
    // Fraction of the grass it is expected to cut that is cut (0..1), by fine cells.
    [[nodiscard]] double progress() const;
    // Belief at a field point, for drawing what the machine knows.
    [[nodiscard]] Belief belief_at(double x, double y) const;
    [[nodiscard]] bool cut_at(double x, double y) const;
    // True when the scanner's current fan reaches the point unobstructed.
    [[nodiscard]] bool sees(double x, double y) const;
    // The tape still to be driven (x, y; heading unused).
    [[nodiscard]] const std::vector<Pose>& path() const {
        return path_;
    }
    // The cut grid: always 0.05 m cells, whatever resolution the obstacles came in.
    [[nodiscard]] int cells_x() const {
        return cells_x_;
    }
    [[nodiscard]] int cells_y() const {
        return cells_y_;
    }
    [[nodiscard]] const std::vector<std::uint8_t>& cut() const {
        return cut_;
    }
    // The fine cells under the deck at a pose, cut now.
    int cut_under(const Pose& pose);
    // How the job is going, for benchmarks: changes of gear, touches, choices made.
    [[nodiscard]] int gear_changes() const {
        return reversals_;
    }
    [[nodiscard]] int bumps() const {
        return bumps_;
    }
    [[nodiscard]] int elections() const {
        return elections_;
    }

  private:
    enum class Kind : std::uint8_t { none, contour, lap, stripe, tuft, escape, goal };
    struct Spot {
        double x{};
        double y{};
    };
    struct RunPoint {
        double x{};
        double y{};
        bool ok{};
    };
    // One entry on the menu: a line with grass still under it.
    struct Run {
        std::vector<RunPoint> pts{};
        int tier{};
        bool closed{};
        bool one_way{};
        Kind kind{Kind::none};
        int frame{-1};
        int lane{};
        int sides{-1};  // stripes: how many neighbouring lanes still stand (-1: not counted yet)
    };
    struct TapePoint {
        double x{};
        double y{};
        int dir{1};    // +1 forward, -1 reverse
        bool work{};   // part of the line itself, not of the way to it
        bool based{};  // the lean has recorded where this point first lay
        double base_x{};
        double base_y{};
        double normal_x{};
        double normal_y{};
    };
    struct Germ {
        int id{};
        double x{};
        double y{};
        int cell{};
        double spacing{};
        int frame{};
        double area{};
        double tested{};
        int splits{};
        double todo{};
        double centre_x{};
        double centre_y{};
        double axis{};
        double longer{};
        double shorter{};
        int far{-1};
        double far_distance{};
    };
    struct Curve {
        std::vector<Spot> pts{};
        double len{};
        double bend{};
        double cost{};
        bool valid{};
    };
    struct Arc {
        std::vector<Spot> pts{};
        Spot end{};
        double heading{};
        double len{};
        bool valid{};
    };
    struct Piece {
        std::vector<Spot> pts{};
        int dir{1};
    };
    struct Move {
        std::vector<Piece> pieces{};
        double cost{};
        int cusps{};
        bool valid{};
    };
    struct Offer {
        double score{};
        double travel{};
        double dir{};
        int end{};
        bool rev{};
        bool valid{};
    };
    // Where an offer is judged from: a pose, and what was last being done there.
    struct Stance {
        double x{};
        double y{};
        double heading{};
        bool has_last{};
        int last_frame{};
        int last_lane{};
        Kind kind{Kind::none};
        bool crow{};  // distances as the crow flies from here, not through the lawn
    };
    struct Job {
        int tier{};
        Kind kind{Kind::none};
        int lane{};
        int frame{-1};
        bool valid{};
    };
    struct Pick {
        Job job{};
        std::vector<TapePoint> tape{};
        std::vector<int> tour_keys{};
        bool tour{};
        bool valid{};
    };
    struct Avoid {
        double x{};
        double y{};
        double until{};
    };
    struct Elected {
        double time{};
        double x{};
        double y{};
        double end_x{};
        double end_y{};
        bool valid{};
    };
    struct Count {
        int pieces{};
        int len{};
    };
    struct Contour {
        std::vector<Spot> pts{};
        bool closed{};
    };

    // the machine
    [[nodiscard]] int cell_of(double x, double y) const;
    [[nodiscard]] double range_at(double off) const;
    [[nodiscard]] bool blocked(double x, double y) const;
    void sense();
    void bump(double x, double y);
    int mow(double x, double y, double heading);
    bool steer(double heading, double distance, double speed, double dt);
    // the manager
    void step(double dt);
    void track(double dt);
    void next();
    void watch();
    void lean();
    [[nodiscard]] int lean_count(double x, double y, double nx, double ny, double lo, double hi) const;
    void take(Pick& pick);
    void escape_with(const std::vector<Spot>& pts);
    bool plan_goal();
    [[nodiscard]] bool can_stand(double x, double y) const;
    int take_steps(double seconds);
    void publish_path();
    // the planner
    void learn();
    void survey();
    void fields();
    [[nodiscard]] double spacing(int cell) const;
    void nucleate();
    void index_germs();
    void own();
    [[nodiscard]] int frame_of(int cell) const;
    [[nodiscard]] Count pieces(const Germ& germ, double axis) const;
    bool self_test();
    [[nodiscard]] bool todo_at(double x, double y, double nx, double ny) const;
    void add_runs(const std::vector<RunPoint>& pts, int tier, bool closed, std::vector<Run>& out, const Run& meta) const;
    [[nodiscard]] std::vector<Contour> contours(double level, const std::vector<float>& field) const;
    void shunned();
    void laps(double level, bool second, std::vector<Run>& runs);
    void build_runs();
    void choose_axis();
    void scout(std::vector<Run>& runs) const;
    [[nodiscard]] bool standing(const std::vector<std::uint8_t>& reach, int cell) const;
    void tufts(std::vector<Run>& runs) const;
    // traversal
    [[nodiscard]] bool passable(int cell) const;
    void transit();
    [[nodiscard]] std::vector<float> spread(int start, const std::vector<int>& wanted) const;
    [[nodiscard]] bool clear_line(double ax, double ay, double bx, double by, bool wide) const;
    [[nodiscard]] std::vector<Spot> route(int cell, bool wide) const;
    // moves
    [[nodiscard]] bool room_at(double x, double y) const;
    [[nodiscard]] Curve hermite(Spot p0, double h0, Spot p1, double h1) const;
    [[nodiscard]] Curve curve(Spot p0, double h0, Spot p1, double h1) const;
    [[nodiscard]] Arc arc(Spot p, double heading, int turn, double angle, int dir, double radius) const;
    [[nodiscard]] Arc back(Spot p, double heading, double distance) const;
    [[nodiscard]] Move hop(Spot p0, double h0, Spot p1, double h1) const;
    [[nodiscard]] Move transition(Spot p0, double h0, Spot p1, double h1) const;
    // choosing
    [[nodiscard]] double distance_from(const Stance& stance, double x, double y) const;
    [[nodiscard]] double turn_for(const Stance& stance, double bearing, double gap, double angle) const;
    [[nodiscard]] bool lane_open(double x, double y, double nx, double ny) const;
    Offer offer(Run& run, const Stance& stance);
    [[nodiscard]] std::vector<RunPoint> ordered(const Run& run, const Offer& offer) const;
    [[nodiscard]] int after(const Run& run, const Offer& taken, const Stance& start, int skip_a, int skip_b, Offer& found);
    Pick tour();
    Pick elect();

    static constexpr long long step_us = 20000;

    FieldSetup field_;
    Machine machine_;
    double width_{};
    double height_{};
    int nx_{};       // planner grid, 0.1 m
    int ny_{};
    int cells_{};    // nx_ * ny_
    int cells_x_{};  // cut grid, 0.05 m
    int cells_y_{};
    std::vector<std::uint8_t> hard_{};      // planner cells: 1 where even a held mower cannot go (empty: as truth_)
    std::vector<std::uint8_t> truth_{};     // planner cells: 1 solid (with the fence)
    std::vector<std::uint8_t> belief_{};    // planner cells: 0 unknown, 1 free, 2 solid
    std::vector<std::uint8_t> cut_{};       // fine cells: 1 cut
    std::vector<std::uint8_t> cuttable_{};  // planner cells it is fair to expect cut
    int cuttable_count_{};
    int cut_count_{};
    Pose pose_{};
    // what has been learned
    std::vector<Germ> germs_{};
    int next_germ_{};
    std::vector<int> germ_index_{};  // germ id -> place in germs_, or -1
    std::vector<int> owner_{};       // planner cells: owning germ id, or -1
    std::vector<int> frozen_{};      // the owner mown ground keeps, or -1
    std::vector<double> frames_{};   // sweep axes; frames_[0] is the lawn's own
    std::vector<float> clear_{};       // distance to anything known solid
    std::vector<float> demand_{};      // how closely the ground here wants looking at
    std::vector<float> grad_x_{};      // unit gradient of clear_: straight away from what is solid
    std::vector<float> grad_y_{};
    std::vector<float> fence_dist_{};  // distance to the fence as seen so far
    std::vector<float> edge_dist_{};   // distance to the fence and whatever stands against it
    std::vector<std::uint8_t> outer_{};      // the fence is the nearest solid thing
    std::vector<std::uint8_t> near_seen_{};  // within half a metre of ground seen free
    std::vector<std::uint8_t> shun_{};       // left alone for a while after a touch
    std::vector<double> crowd_{};  // per cell, what the wavefronts charge: for crossing near things,
    std::vector<double> grow_{};   // for an owner growing near them,
    std::vector<double> wary_{};   // and for growing straight at them
    std::vector<float> travel_{};            // cost to drive here from the mower
    std::vector<int> travel_from_{};
    std::vector<Run> runs_{};
    bool belief_dirty_{true};
    bool axis_locked_{};
    std::vector<Avoid> avoid_{};
    std::map<int, int> tuft_tries_{};
    // what is being done
    std::vector<TapePoint> tape_{};
    int at_{};
    Job cur_{};
    Job job_{};
    std::vector<TapePoint> preview_{};
    bool held_{};
    double hold_x_{};
    double hold_y_{};
    bool finished_{};
    double turn_{};  // rate of turn, radians per second
    double time_{};
    double tick_{};
    int reversals_{};
    int bumps_{};
    int elections_{};
    int escapes_{};
    bool has_last_stripe_{};
    int last_frame_{};
    int last_lane_{};
    Kind last_kind_{Kind::none};
    Elected last_elect_{};
    bool has_goal_{};
    double goal_x_{};
    double goal_y_{};
    int goal_tries_{};
    double goal_best_{};
    // the step being reported
    double moved_{};
    bool touched_{};
    long long pending_us_{};
    std::vector<Pose> path_{};
};

} // namespace coverage
