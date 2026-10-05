#pragma once
// What an ambient scene remembers between sessions: whether it was paused and its
// detail. The text form is a header line and `key value` lines; unknown keys are
// ignored so later versions can add some, and a damaged file decodes to defaults.
#include "present.hpp"

#include <string>

namespace ambient {

struct Settings {
    bool paused{};
    Detail detail{Detail::balanced};
};

[[nodiscard]] std::string encode_settings(const Settings& settings);
// Returns false, leaving `destination` unchanged, when the text is not settings.
[[nodiscard]] bool decode_settings(const std::string& text, Settings& destination);
[[nodiscard]] const char* detail_name(Detail detail);

} // namespace ambient
