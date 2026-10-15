// Times each part of preparing a library file: the tables decoded and loaded, the
// packer made, the base decoded and checked. Not shipped.
//
//   prepare_lab <library file>
#include "../src/container.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string_view>
#include <vector>

namespace {

double milliseconds_since(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() * 1000;
}

}  // namespace

int main(int, char** argv) {
    std::ifstream file(argv[1], std::ios::binary);
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    pl::LibraryFile library;
    if (!library.open(bytes))
        return 1;
    // The parts, as prepare() does them, from the file's own table.
    std::size_t at = 16;
    std::uint32_t tables_length = 0, tables_offset = 0, tables_code = 0;
    for (int i = 0; i < 4; ++i) {
        tables_length |= static_cast<std::uint32_t>(bytes[at + static_cast<std::size_t>(i)]) << (8 * i);
        tables_offset |= static_cast<std::uint32_t>(bytes[at + 4 + static_cast<std::size_t>(i)]) << (8 * i);
        tables_code |= static_cast<std::uint32_t>(bytes[at + 8 + static_cast<std::size_t>(i)]) << (8 * i);
    }
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    std::vector<std::uint8_t> saved;
    if (!pl::lz_decode(std::span<const std::uint8_t>(bytes.data() + tables_offset, tables_code), tables_length, saved))
        return 1;
    const double decode_tables = milliseconds_since(start);
    start = std::chrono::steady_clock::now();
    pl::PackTables tables;
    if (!pl::load_tables(std::string_view(reinterpret_cast<const char*>(saved.data()), saved.size()), tables))
        return 1;
    const double load = milliseconds_since(start);
    start = std::chrono::steady_clock::now();
    const pl::TextPacker packer(tables);
    const double make_packer = milliseconds_since(start);
    const pl::SharedPart& base_part = library.base();
    start = std::chrono::steady_clock::now();
    pl::LzBase base;
    if (!pl::lz_decode_base(std::span<const std::uint8_t>(bytes.data() + base_part.offset, base_part.code_length),
                            base_part.length, base))
        return 1;
    const double decode_base = milliseconds_since(start);
    start = std::chrono::steady_clock::now();
    const bool checked = pl::sha256(base.text()) == base_part.digest;
    const double check = milliseconds_since(start);
    start = std::chrono::steady_clock::now();
    if (!library.prepare())
        return 1;
    const double whole = milliseconds_since(start);
    std::printf("tables %u bytes: decoded %.1f ms, loaded %.1f ms (%zu words); packer made %.1f ms; base %u bytes "
                "decoded %.1f ms, checked %.1f ms %s; prepare() %.1f ms\n",
                tables_length, decode_tables, load, tables.words.size(), make_packer, base_part.length, decode_base,
                check, checked ? "" : "MISMATCH", whole);
    return 0;
}
