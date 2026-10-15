#include "container.hpp"

#include "lz_coder.hpp"

#include <algorithm>
#include <cstring>

namespace pl {
namespace {

constexpr std::uint32_t format_version = 2;
constexpr std::uint32_t coder_lz = 1;

void put_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int i = 0; i < 4; ++i)
        out.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFU));
}

bool get_u32(std::span<const std::uint8_t> bytes, std::size_t& at, std::uint32_t& value) {
    if (at + 4 > bytes.size())
        return false;
    value = 0;
    for (int i = 0; i < 4; ++i)
        value |= static_cast<std::uint32_t>(bytes[at + static_cast<std::size_t>(i)]) << (8 * i);
    at += 4;
    return true;
}

std::span<const std::uint8_t> bytes_of(const std::string& text) {
    const std::span<const std::uint8_t> view(reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
    return view;
}

}  // namespace

bool LibraryFile::open(std::vector<std::uint8_t> bytes) {
    bytes_.clear();
    books_.clear();
    const std::span<const std::uint8_t> view(bytes);
    if (view.size() < 16 || std::memcmp(view.data(), "PLIB", 4) != 0)
        return false;
    std::size_t at = 4;
    std::uint32_t version = 0, count = 0, coder = 0;
    if (!get_u32(view, at, version) || !get_u32(view, at, count) || !get_u32(view, at, coder))
        return false;
    if (version != format_version || coder != coder_lz || count > 1000)
        return false;
    std::vector<ShelvedBook> books;
    for (std::uint32_t n = 0; n < count; ++n) {
        if (at >= view.size())
            return false;
        const std::size_t id_length = view[at];
        ++at;
        if (at + id_length > view.size())
            return false;
        ShelvedBook book;
        book.id.assign(reinterpret_cast<const char*>(view.data() + at), id_length);
        at += id_length;
        if (!get_u32(view, at, book.length) || !get_u32(view, at, book.offset) ||
            !get_u32(view, at, book.code_length))
            return false;
        if (at + book.digest.size() > view.size())
            return false;
        std::memcpy(book.digest.data(), view.data() + at, book.digest.size());
        at += book.digest.size();
        if (at + 2 > view.size())
            return false;
        const std::size_t shelf_length = static_cast<std::size_t>(view[at]) | (static_cast<std::size_t>(view[at + 1]) << 8);
        at += 2;
        if (at + shelf_length > view.size())
            return false;
        book.shelf.assign(reinterpret_cast<const char*>(view.data() + at), shelf_length);
        at += shelf_length;
        const std::uint64_t end = static_cast<std::uint64_t>(book.offset) + book.code_length;
        if (end > view.size())
            return false;
        books.push_back(book);
    }
    bytes_ = std::move(bytes);
    books_ = std::move(books);
    return true;
}

bool LibraryFile::read(std::size_t index, std::string& out) const {
    out.clear();
    if (index >= books_.size())
        return false;
    const ShelvedBook& book = books_[index];
    const std::span<const std::uint8_t> code(bytes_.data() + book.offset, book.code_length);
    std::vector<std::uint8_t> text;
    if (!lz_decode(code, book.length, text))
        return false;
    if (sha256(text) != book.digest)
        return false;
    out.assign(reinterpret_cast<const char*>(text.data()), text.size());
    return true;
}

void write_library(const std::vector<BookText>& books, std::vector<std::uint8_t>& out) {
    out.clear();
    std::vector<std::vector<std::uint8_t>> codes(books.size());
    for (std::size_t n = 0; n < books.size(); ++n)
        lz_encode(bytes_of(books[n].text), codes[n]);
    std::size_t table = 16;
    for (const BookText& book : books)
        table += 1 + book.id.size() + 12 + 32 + 2 + std::min<std::size_t>(book.shelf.size(), 65535);
    out.insert(out.end(), {'P', 'L', 'I', 'B'});
    put_u32(out, format_version);
    put_u32(out, static_cast<std::uint32_t>(books.size()));
    put_u32(out, coder_lz);
    std::size_t offset = table;
    for (std::size_t n = 0; n < books.size(); ++n) {
        out.push_back(static_cast<std::uint8_t>(books[n].id.size()));
        out.insert(out.end(), books[n].id.begin(), books[n].id.end());
        put_u32(out, static_cast<std::uint32_t>(books[n].text.size()));
        put_u32(out, static_cast<std::uint32_t>(offset));
        put_u32(out, static_cast<std::uint32_t>(codes[n].size()));
        const Sha256Digest digest = sha256(bytes_of(books[n].text));
        out.insert(out.end(), digest.begin(), digest.end());
        const std::size_t shelf_length = std::min<std::size_t>(books[n].shelf.size(), 65535);
        out.push_back(static_cast<std::uint8_t>(shelf_length & 0xFFU));
        out.push_back(static_cast<std::uint8_t>(shelf_length >> 8));
        out.insert(out.end(), books[n].shelf.begin(), books[n].shelf.begin() + static_cast<std::ptrdiff_t>(shelf_length));
        offset += codes[n].size();
    }
    for (const std::vector<std::uint8_t>& code : codes)
        out.insert(out.end(), code.begin(), code.end());
}

}  // namespace pl
