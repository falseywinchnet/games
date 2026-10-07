#include "save.hpp"
#include "runtime_paths.hpp"
namespace ct {
std::filesystem::path save_path(bool dev) {
    const char* name = dev ? "catching_thieves-dev-v1.txt" : "catching_thieves-v1.txt";
    return games::state_directory() / name;
}
} // namespace ct
