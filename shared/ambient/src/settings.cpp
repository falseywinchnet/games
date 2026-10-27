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

std::string setting(const Settings& settings, const std::string& key, const std::string& fallback) {
    for (const SettingLine& line : settings.extra) {
        if (line.key == key)
            return line.value;
    }
    return fallback;
}

void set_setting(Settings& settings, const std::string& key, const std::string& value) {
    for (SettingLine& line : settings.extra) {
        if (line.key == key) {
            line.value = value;
            return;
        }
    }
    settings.extra.push_back({key, value});
}

std::string encode_settings(const Settings& settings) {
    std::string text = header;
    text += "\npaused ";
    text += settings.paused ? "1" : "0";
    text += "\ndetail ";
    text += detail_name(settings.detail);
    text += "\n";
    for (const SettingLine& line : settings.extra) {
        if (line.key.empty() || line.key.find_first_of(" \n") != std::string::npos ||
            line.value.find('\n') != std::string::npos)
            continue;
        text += line.key + " " + line.value + "\n";
    }
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
        } else if (key != "paused" && !key.empty() && settings.extra.size() < 64) {
            settings.extra.push_back({key, value});
        }
    }
    destination = settings;
    return true;
}

} // namespace ambient
