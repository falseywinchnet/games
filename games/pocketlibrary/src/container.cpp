#include "container.hpp"

#include <algorithm>
#include <cstring>
#include <string_view>

namespace pl {
namespace {

constexpr std::uint32_t format_version = 3;
constexpr std::uint32_t coder_packed_lz = 2;
constexpr std::size_t part_bytes = 12 + 32;

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

bool get_digest(std::span<const std::uint8_t> bytes, std::size_t& at, Sha256Digest& digest) {
    if (at + digest.size() > bytes.size())
        return false;
    std::memcpy(digest.data(), bytes.data() + at, digest.size());
    at += digest.size();
    return true;
}

bool get_part(std::span<const std::uint8_t> bytes, std::size_t& at, SharedPart& part) {
    if (!get_u32(bytes, at, part.length) || !get_u32(bytes, at, part.offset) || !get_u32(bytes, at, part.code_length) ||
        !get_digest(bytes, at, part.digest))
        return false;
    return static_cast<std::uint64_t>(part.offset) + part.code_length <= bytes.size();
}

void put_part(std::vector<std::uint8_t>& out, const SharedPart& part) {
    put_u32(out, part.length);
    put_u32(out, part.offset);
    put_u32(out, part.code_length);
    out.insert(out.end(), part.digest.begin(), part.digest.end());
}

std::span<const std::uint8_t> bytes_of(std::string_view text) {
    const std::span<const std::uint8_t> view(reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
    return view;
}

// The cache's key for a code: what is coded, how, and after what.
Sha256Digest cache_key(std::string_view kind, int effort, const Sha256Digest& after, std::string_view text) {
    Sha256 hash;
    hash.update(bytes_of(kind));
    const std::string how = " lz format " + std::to_string(lz_format) + " effort " + std::to_string(effort) + " ";
    hash.update(bytes_of(how));
    hash.update(after);
    hash.update(bytes_of(text));
    return hash.finish();
}

}  // namespace

LibraryFile::LibraryFile() = default;
LibraryFile::~LibraryFile() = default;

bool LibraryFile::open(std::vector<std::uint8_t> bytes) {
    bytes_.clear();
    books_.clear();
    packer_.reset();
    base_ = LzBase{};
    prepared_ = Prepared::no;
    const std::span<const std::uint8_t> view(bytes);
    if (view.size() < 16 || std::memcmp(view.data(), "PLIB", 4) != 0)
        return false;
    std::size_t at = 4;
    std::uint32_t version = 0, count = 0, coder = 0;
    if (!get_u32(view, at, version) || !get_u32(view, at, count) || !get_u32(view, at, coder))
        return false;
    if (version != format_version || coder != coder_packed_lz || count > 4000)
        return false;
    SharedPart tables, base;
    if (!get_part(view, at, tables) || !get_part(view, at, base))
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
        if (!get_u32(view, at, book.length) || !get_u32(view, at, book.packed_length) || at >= view.size())
            return false;
        const std::uint8_t where = view[at];
        ++at;
        if (where > 1)
            return false;
        book.in_base = where == 1;
        if (!get_u32(view, at, book.offset) || !get_u32(view, at, book.code_length) ||
            !get_digest(view, at, book.digest))
            return false;
        if (at + 2 > view.size())
            return false;
        const std::size_t shelf_length = static_cast<std::size_t>(view[at]) | (static_cast<std::size_t>(view[at + 1]) << 8);
        at += 2;
        if (at + shelf_length > view.size())
            return false;
        book.shelf.assign(reinterpret_cast<const char*>(view.data() + at), shelf_length);
        at += shelf_length;
        const std::uint64_t end = static_cast<std::uint64_t>(book.offset) +
                                  (book.in_base ? book.packed_length : book.code_length);
        if (end > (book.in_base ? base.length : view.size()))
            return false;
        books.push_back(book);
    }
    bytes_ = std::move(bytes);
    books_ = std::move(books);
    tables_part_ = tables;
    base_part_ = base;
    return true;
}

std::span<const std::uint8_t> LibraryFile::code_of(const SharedPart& part) const {
    return {bytes_.data() + part.offset, part.code_length};
}

bool LibraryFile::prepare() {
    if (prepared_ != Prepared::no)
        return prepared_ == Prepared::yes;
    prepared_ = Prepared::damaged;
    if (bytes_.empty())
        return false;
    std::vector<std::uint8_t> saved;
    if (!lz_decode(code_of(tables_part_), tables_part_.length, saved) || sha256(saved) != tables_part_.digest)
        return false;
    const std::string_view saved_view(reinterpret_cast<const char*>(saved.data()), saved.size());
    PackTables tables;
    if (!load_tables(saved_view, tables))
        return false;
    LzBase base;
    if (base_part_.length > 0) {
        if (!lz_decode_base(code_of(base_part_), base_part_.length, base) ||
            sha256(base.text()) != base_part_.digest)
            return false;
    }
    packer_ = std::make_unique<TextPacker>(std::move(tables));
    base_ = base;
    prepared_ = Prepared::yes;
    return true;
}

bool LibraryFile::read(std::size_t index, std::string& out) {
    out.clear();
    if (index >= books_.size() || !prepare())
        return false;
    const ShelvedBook& book = books_[index];
    std::string_view packed;
    std::vector<std::uint8_t> decoded;
    if (book.in_base) {
        const std::span<const std::uint8_t> base = base_.text();
        packed = std::string_view(reinterpret_cast<const char*>(base.data()) + book.offset, book.packed_length);
    } else {
        const std::span<const std::uint8_t> code(bytes_.data() + book.offset, book.code_length);
        if (!lz_decode(code, book.packed_length, decoded, base_))
            return false;
        packed = std::string_view(reinterpret_cast<const char*>(decoded.data()), decoded.size());
    }
    out.reserve(book.length);
    if (!(*packer_).unpack(packed, out) || out.size() != book.length || sha256(bytes_of(out)) != book.digest) {
        out.clear();
        return false;
    }
    return true;
}

bool write_library(const std::vector<BookText>& books, const LibraryPlan& plan, std::vector<std::uint8_t>& out) {
    out.clear();
    // The tables, from every book.
    std::vector<std::string_view> texts;
    texts.reserve(books.size());
    for (const BookText& book : books)
        texts.push_back(book.text);
    const PackTables tables = learn_tables(texts);
    const std::string saved = save_tables(tables);
    const TextPacker packer(tables);
    std::vector<std::string> packed(books.size());
    for (std::size_t n = 0; n < books.size(); ++n)
        if (!packer.pack(books[n].text, packed[n]))
            return false;

    // The base: the planned books' packed texts, end to end.
    std::vector<bool> in_base(books.size(), false);
    std::vector<std::uint32_t> base_offset(books.size(), 0);
    std::string base_text;
    for (const std::string& id : plan.base) {
        for (std::size_t n = 0; n < books.size(); ++n) {
            if (books[n].id != id || in_base[n])
                continue;
            in_base[n] = true;
            base_offset[n] = static_cast<std::uint32_t>(base_text.size());
            base_text += packed[n];
        }
    }
    // Then a cross-section: every so many books of the shelf, the stride growing until
    // the chosen books fit.
    std::size_t outside = 0;
    for (std::size_t n = 0; n < books.size(); ++n)
        outside += in_base[n] ? 0 : 1;
    if (plan.base_bytes > base_text.size() && outside > 0) {
        std::size_t left = 0;
        for (std::size_t n = 0; n < books.size(); ++n)
            left += in_base[n] ? 0 : packed[n].size();
        const std::size_t room = plan.base_bytes - base_text.size();
        // The share of the shelf that fits, as one book in `stride`.
        const double share = static_cast<double>(room) / static_cast<double>(std::max<std::size_t>(left, 1));
        const double stride = share >= 1 ? 1.0 : 1.0 / share;
        double next = 0;
        std::size_t seen = 0;
        for (std::size_t n = 0; n < books.size(); ++n) {
            if (in_base[n])
                continue;
            if (static_cast<double>(seen++) < next)
                continue;
            next += stride;
            if (base_text.size() + packed[n].size() > plan.base_bytes)
                continue;
            in_base[n] = true;
            base_offset[n] = static_cast<std::uint32_t>(base_text.size());
            base_text += packed[n];
        }
        // What the stride left of the room, filled in shelf order.
        for (std::size_t n = 0; n < books.size(); ++n) {
            if (in_base[n] || base_text.size() + packed[n].size() > plan.base_bytes)
                continue;
            in_base[n] = true;
            base_offset[n] = static_cast<std::uint32_t>(base_text.size());
            base_text += packed[n];
        }
    }
    const Sha256Digest base_digest = sha256(bytes_of(base_text));
    const Sha256Digest none{};

    // The codes: from the cache where it has them.
    std::vector<std::uint8_t> tables_code, base_code;
    const Sha256Digest tables_key = cache_key("tables", plan.effort, none, saved);
    if (plan.cache == nullptr || !(*plan.cache).find(tables_key, tables_code)) {
        lz_encode(bytes_of(saved), tables_code, plan.effort);
        if (plan.cache != nullptr)
            (*plan.cache).keep(tables_key, tables_code);
    }
    std::vector<std::vector<std::uint8_t>> codes(books.size());
    std::vector<Sha256Digest> keys(books.size());
    bool all_kept = plan.cache != nullptr;
    const Sha256Digest base_key = cache_key("base", plan.effort, none, base_text);
    if (all_kept)
        all_kept = base_text.empty() || (*plan.cache).find(base_key, base_code);
    for (std::size_t n = 0; n < books.size(); ++n) {
        if (in_base[n])
            continue;
        keys[n] = cache_key("book", plan.effort, base_digest, packed[n]);
        if (plan.cache != nullptr && !(*plan.cache).find(keys[n], codes[n]))
            all_kept = false;
    }
    if (!all_kept) {
        // The encoder's base (its model and its match tree) is needed for any book
        // coded after it, so it is made again even when its code was kept.
        LzBase base;
        if (!base_text.empty()) {
            base_code.clear();
            lz_encode_base(bytes_of(base_text), base_code, plan.effort, base);
            if (plan.cache != nullptr)
                (*plan.cache).keep(base_key, base_code);
        }
        for (std::size_t n = 0; n < books.size(); ++n) {
            if (in_base[n] || !codes[n].empty())
                continue;
            lz_encode(bytes_of(packed[n]), codes[n], plan.effort, base);
            if (plan.cache != nullptr)
                (*plan.cache).keep(keys[n], codes[n]);
        }
    }

    // The file.
    std::size_t table = 16 + 2 * part_bytes;
    for (const BookText& book : books)
        table += 1 + book.id.size() + 8 + 1 + 8 + 32 + 2 + std::min<std::size_t>(book.shelf.size(), 65535);
    out.insert(out.end(), {'P', 'L', 'I', 'B'});
    put_u32(out, format_version);
    put_u32(out, static_cast<std::uint32_t>(books.size()));
    put_u32(out, coder_packed_lz);
    std::size_t offset = table;
    SharedPart tables_part{static_cast<std::uint32_t>(saved.size()), static_cast<std::uint32_t>(offset),
                           static_cast<std::uint32_t>(tables_code.size()), sha256(bytes_of(saved))};
    offset += tables_code.size();
    SharedPart base_part{static_cast<std::uint32_t>(base_text.size()), static_cast<std::uint32_t>(offset),
                         static_cast<std::uint32_t>(base_code.size()), base_digest};
    offset += base_code.size();
    put_part(out, tables_part);
    put_part(out, base_part);
    for (std::size_t n = 0; n < books.size(); ++n) {
        out.push_back(static_cast<std::uint8_t>(books[n].id.size()));
        out.insert(out.end(), books[n].id.begin(), books[n].id.end());
        put_u32(out, static_cast<std::uint32_t>(books[n].text.size()));
        put_u32(out, static_cast<std::uint32_t>(packed[n].size()));
        out.push_back(in_base[n] ? 1 : 0);
        put_u32(out, in_base[n] ? base_offset[n] : static_cast<std::uint32_t>(offset));
        put_u32(out, static_cast<std::uint32_t>(codes[n].size()));
        const Sha256Digest digest = sha256(bytes_of(books[n].text));
        out.insert(out.end(), digest.begin(), digest.end());
        const std::size_t shelf_length = std::min<std::size_t>(books[n].shelf.size(), 65535);
        out.push_back(static_cast<std::uint8_t>(shelf_length & 0xFFU));
        out.push_back(static_cast<std::uint8_t>(shelf_length >> 8));
        out.insert(out.end(), books[n].shelf.begin(), books[n].shelf.begin() + static_cast<std::ptrdiff_t>(shelf_length));
        offset += codes[n].size();
    }
    out.insert(out.end(), tables_code.begin(), tables_code.end());
    out.insert(out.end(), base_code.begin(), base_code.end());
    for (const std::vector<std::uint8_t>& code : codes)
        out.insert(out.end(), code.begin(), code.end());
    return true;
}

}  // namespace pl
