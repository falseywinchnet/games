// The hosted-view contract (new-games/kit/view_contract_test.hpp) run against Canopy
// Falls' real control. Built with the PlaySuite application.
//
// The falls move whenever they are visible, so the scene never settles untouched. It
// must still stop completely when hidden.
#include "falls_view.hpp"
#include "view_contract_test.hpp"

int main(int argc, char** argv) {
    kit::ContractOptions options;
    options.settles_when_untouched = false;
    if (argc > 1) {
        options.preview_directory = argv[1];
    }
    const ambient::ViewOptions view_options{.hosted = true, .dev = true};
    return kit::run_view_contract<cf::FallsView, ambient::ViewOptions>("canopyfalls", view_options, options);
}
