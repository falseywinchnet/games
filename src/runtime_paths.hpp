#pragma once
#include <filesystem>
#include <string>
namespace games {
void initialize_assets(const char* executable);
std::string asset_directory();
std::filesystem::path state_directory();
}
