// The prepared deck decodes from compressed PNG to exactly the premultiplied pixels the preparation recorded
// (tools/prepare_portable_assets.py writes cards/verification.tsv), on one thread and on several, and
// damaged or unsupported files are refused rather than drawn.
#include "inflate.hpp"
#include "platform/image.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message.c_str());
        std::exit(1);
    }
}
std::vector<std::uint8_t> read_file(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream bytes;
    bytes << input.rdbuf();
    const std::string text = bytes.str();
    return std::vector<std::uint8_t>(text.begin(), text.end());
}
struct Card {
    std::string file;
    int width = 0, height = 0;
    std::uint32_t crc = 0;
};
}  // namespace

int main(int argc, char** argv) {
    require(argc == 2, "Pass the prepared runtime asset directory");
    const std::string cards = std::string(argv[1]) + "/cards/";
    std::ifstream contract(cards + "verification.tsv");
    std::size_t count = 0;
    require(static_cast<bool>(contract >> count), "Card verification contract is readable");
    std::vector<Card> deck(count);
    for (Card& card : deck) {
        std::string crc;
        require(static_cast<bool>(contract >> card.file >> card.width >> card.height >> crc), "Complete contract row");
        card.crc = static_cast<std::uint32_t>(std::stoul(crc, nullptr, 16));
    }
    require(count >= 50, "The whole hanafuda deck, its back and its shadow are listed");

    std::vector<std::string> paths;
    std::vector<kk::Canvas> parallel(count);
    std::vector<kk::Canvas*> targets;
    for (std::size_t i = 0; i < count; ++i) {
        const Card& card = deck[i];
        kk::Canvas canvas;
        require(kk::load_png(cards + card.file, canvas), "Decode " + card.file);
        require(canvas.w == card.width && canvas.h == card.height, "Recorded size of " + card.file);
        require(ambient::crc32(canvas.px) == card.crc, "Premultiplied BGRA pixels of " + card.file + " match the preparation");
        paths.push_back(cards + card.file);
        targets.push_back(&parallel[i]);
    }
    require(kk::load_pngs(paths, targets) == static_cast<int>(count), "Every card loads on worker threads");
    for (std::size_t i = 0; i < count; ++i)
        require(ambient::crc32(parallel[i].px) == deck[i].crc, "Threaded decode of " + deck[i].file + " is identical");

    // Damage is refused, and the canvas is left untouched.
    const std::vector<std::uint8_t> original = read_file(cards + deck[0].file);
    kk::Canvas untouched;
    untouched.resize(1, 1);
    std::vector<std::uint8_t> truncated(original.begin(), original.end() - 20);
    require(!kk::decode_png(truncated, untouched) && untouched.w == 1, "A truncated file is refused");
    std::vector<std::uint8_t> flipped = original;
    flipped[flipped.size() / 2] ^= 0x40;
    require(!kk::decode_png(flipped, untouched) && untouched.w == 1, "Corrupt image data is refused");
    std::vector<std::uint8_t> header = original;
    header[8 + 8 + 9] = 2;  // IHDR colour type: RGB instead of RGBA, with a valid chunk checksum
    const std::uint32_t crc = ambient::crc32(std::span<const std::uint8_t>(header.data() + 12, 17));
    for (int i = 0; i < 4; ++i) header[29 + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(crc >> (24 - 8 * i));
    require(!kk::decode_png(header, untouched) && untouched.w == 1, "Only the prepared RGBA layout is accepted");
    std::vector<std::uint8_t> extra = original;
    extra.push_back(0);
    require(!kk::decode_png(extra, untouched) && untouched.w == 1, "Trailing bytes are refused");
    require(!kk::load_png(cards + "missing.png", untouched), "A missing file is refused");
    std::printf("%zu prepared cards decode to their recorded pixels\n", count);
    return 0;
}
