// The hosted-view contract (new-games/kit/view_contract_test.hpp) run against this
// game's real control. Built with the PlaySuite application; see cmake/Application.cmake.
#include "tumbledstones_view.hpp"
#include "view_contract_test.hpp"

int main(int argc, char** argv) {
    kit::ContractOptions options;
    if (argc > 1) {
        options.preview_directory = argv[1];
    }
    const ts::Options view_options{.hosted = true, .dev = true};
    const int result = kit::run_view_contract<ts::TumbledStonesView, ts::Options>("tumbledstones", view_options, options);
    return result;
}
