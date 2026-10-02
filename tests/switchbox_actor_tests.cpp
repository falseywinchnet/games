#include "actor.hpp"
#include <cassert>

int main() {
    sbx::Actor actor(3);
    sbx::StageState state{};
    state.sw[0].on = 1;
    actor.flipped(0, false, false);
    actor.set_hold(0);
    bool carried = false;
    for (int frame = 0; frame < 600; ++frame) {
        actor.update(1.0 / 30, state);
        if (actor.carrying_pointer()) { carried = true; break; }
    }
    assert(carried);
    actor.cancel_pointer_interaction();
    assert(!actor.carrying_pointer());
    sbx::V3 drop{};
    bool dropped = actor.take_pointer_drop(drop);
    assert(!dropped);
    for (int frame = 0; frame < 120; ++frame) {
        actor.update(1.0 / 30, state);
        assert(!actor.carrying_pointer());
        dropped = actor.take_pointer_drop(drop);
        assert(!dropped);
    }
    assert(state.sw[0].on == 0);
}
