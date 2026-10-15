#include "printer.hpp"

#include "game_data.hpp"

#include <optional>
#include <string>

namespace pl {

Printer::Printer()
    : fonts_(std::make_unique<page::FontSet>()), typesetter_(std::make_unique<page::Typesetter>(*fonts_)),
      rasterizer_(std::make_unique<page::PageRasterizer>(*fonts_)) {}

Printer::~Printer() = default;

bool Printer::load() {
    if (tried_)
        return ready_;
    tried_ = true;
    struct Face {
        page::FaceId id;
        const char* file;
    };
    const Face faces[] = {
        {page::FaceId::literata_regular, "pocketlibrary/fonts/Literata.ttf"},
        {page::FaceId::literata_italic, "pocketlibrary/fonts/Literata-Italic.ttf"},
        {page::FaceId::eb_garamond_regular, "pocketlibrary/fonts/EBGaramond-Regular.ttf"},
        {page::FaceId::eb_garamond_italic, "pocketlibrary/fonts/EBGaramond-Italic.ttf"},
    };
    bytes_.reserve(4);
    for (const Face& face : faces) {
        const std::optional<std::string> loaded = games::load_game_asset(face.file);
        if (!loaded || !(*fonts_).ready())
            return false;
        bytes_.emplace_back((*loaded).begin(), (*loaded).end());
        if (!(*fonts_).load(face.id, bytes_.back()))
            return false;
    }
    ready_ = true;
    return true;
}

}  // namespace pl
