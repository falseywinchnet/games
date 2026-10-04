#include "save.hpp"
#include "runtime_paths.hpp"
namespace mz {
std::filesystem::path save_path(bool dev) { return games::state_directory() / (dev ? "maze-dev-v1.txt" : "maze-v1.txt"); }
}
