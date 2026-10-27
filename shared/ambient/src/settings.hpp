#pragma once
// What an ambient scene remembers between sessions: whether it was paused, its
// detail, and any scene-specific choices (a mower's colour). The text form is a
// header line and `key value` lines; keys the engine does not know are kept for the
// scene, and a damaged file decodes to defaults.
#include "present.hpp"

#include <string>
#include <vector>

namespace ambient {

// One scene-specific setting. Keys are single words; values are one line.
struct SettingLine {
    std::string key{};
    std::string value{};
};

struct Settings {
    bool paused{};
    Detail detail{Detail::balanced};
    std::vector<SettingLine> extra{};
};
// The value for `key` in settings.extra, or `fallback`.
[[nodiscard]] std::string setting(const Settings& settings, const std::string& key, const std::string& fallback);
void set_setting(Settings& settings, const std::string& key, const std::string& value);

[[nodiscard]] std::string encode_settings(const Settings& settings);
// Returns false, leaving `destination` unchanged, when the text is not settings.
[[nodiscard]] bool decode_settings(const std::string& text, Settings& destination);
[[nodiscard]] const char* detail_name(Detail detail);

} // namespace ambient
