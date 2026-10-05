// The hosted-view contract (new-games/kit/view_contract_test.hpp) run against
// Stillwater's real control. Built with the PlaySuite application.
//
// Stillwater is ambient: its fish and grass move while it is visible, so it never
// settles untouched (see HANDOFF.md). It must still stop completely when hidden.
#include "stillwater_view.hpp"
#include "view_contract_test.hpp"

int main(int argc, char** argv) {
    kit::ContractOptions options;
    options.settles_when_untouched = false;
    if (argc > 1) {
        options.preview_directory = argv[1];
    }
    const ambient::ViewOptions view_options{.hosted = true, .dev = true};
    const int result =
        kit::run_view_contract<sw::StillwaterView, ambient::ViewOptions>("stillwater", view_options, options);
    return result;
}
