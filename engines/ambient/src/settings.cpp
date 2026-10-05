#include "settings.hpp"

#include <sstream>

namespace ambient {
namespace {

const char* const header = "ambient-settings 1";

} // namespace

const char* detail_name(Detail detail) {
    if (detail == Detail::light)
        return "light";
    if (detail == Detail::fine)
        return "fine";
    return "balanced";
}

std::string encode_settings(const Settings& settings) {
    std::string text = header;
    text += "\npaused ";
    text += settings.paused ? "1" : "0";
    text += "\ndetail ";
    text += detail_name(settings.detail);
    text += "\n";
    return text;
}

bool decode_settings(const std::string& text, Settings& destination) {
    if (text.size() > 4096)
        return false;
    std::istringstream input(text);
    std::string line{};
    if (!std::getline(input, line) || line != header)
        return false;
    Settings settings{};
    while (std::getline(input, line)) {
        const std::size_t space = line.find(' ');
        if (space == std::string::npos)
            continue;
        const std::string key = line.substr(0, space);
        const std::string value = line.substr(space + 1);
        if (key == "paused" && (value == "0" || value == "1"))
            settings.paused = value == "1";
        if (key == "detail") {
            if (value == "light")
                settings.detail = Detail::light;
            if (value == "balanced")
                settings.detail = Detail::balanced;
            if (value == "fine")
                settings.detail = Detail::fine;
        }
    }
    destination = settings;
    return true;
}

} // namespace ambient
