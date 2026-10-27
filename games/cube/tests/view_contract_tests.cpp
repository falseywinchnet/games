// The hosted-view contract (new-games/kit/view_contract_test.hpp) run against Nature
// Cube's real control. Built with the PlaySuite application.
#include "cube_view.hpp"
#include "view_contract_test.hpp"

int main(int argc, char** argv) {
    kit::ContractOptions options;
    if (argc > 1) {
        options.preview_directory = argv[1];
    }
    const ps_cube::Options view_options{.hosted = true, .dev = true};
    const int result = kit::run_view_contract<ps_cube::CubeView, ps_cube::Options>("cube", view_options, options);
    return result;
}
