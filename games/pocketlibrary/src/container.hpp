#pragma once
// The library file: one read-only file holding every book. Little-endian throughout:
//
//   "PLIB", version (u32, 3), book count (u32), coder (u32, 2 = packed, then lz_coder)
//   the packing tables (text_pack.hpp): length (u32), code offset (u32), code length
//             (u32), SHA-256 of the saved tables (32 bytes)
//   the base: length (u32), code offset (u32), code length (u32), SHA-256 (32 bytes)
//   per book: id length (u8), id, text length (u32), packed length (u32),
//             where (u8: 0 coded after the base, 1 inside the base), offset (u32: of its
//             code in the file, or of its packed text in the base), code length (u32,
//             0 inside the base), SHA-256 of the text (32 bytes),
//             shelf record length (u16), shelf record
//   the codes: the tables', the base's, then the books'
//
// Every book is packed with the library's tables (text_pack.hpp) and the packed bytes
// coded by lz_coder. The base is some books' packed texts end to end, coded as one: a
// book inside it is a slice of it, and every other book is coded after it (LzBase), so
// it may refer back to anything the base says and starts with a model already trained.
// The tables and the base are decoded once, the first time a book is read; each other
// book is decoded alone when it is opened. Each book is checked by the SHA-256 of its
// text.
//
// The shelf record is what the bookcase needs without opening the book: lines of
// "key<TAB>value" (title, subtitle, category, binding, leather).
//
// A book's text is its normalized records (book.hpp reads them).
#include "lz_coder.hpp"
#include "sha256.hpp"
#include "text_pack.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace pl {

struct ShelvedBook {
    std::string id;
    std::uint32_t length = 0;         // of the text
    std::uint32_t packed_length = 0;
    bool in_base = false;
    std::uint32_t offset = 0;         // of the code in the file, or of the packed text in the base
    std::uint32_t code_length = 0;    // 0 for a book inside the base
    Sha256Digest digest{};
    std::string shelf;  // the shelf record
};

// Where one of the shared parts (the tables, the base) lies.
struct SharedPart {
    std::uint32_t length = 0;
    std::uint32_t offset = 0;
    std::uint32_t code_length = 0;
    Sha256Digest digest{};
};

class LibraryFile {
  public:
    LibraryFile();
    ~LibraryFile();
    // Takes the file's bytes. False, holding nothing, when the header or the table is
    // damaged or a code lies outside the file. Decodes nothing.
    [[nodiscard]] bool open(std::vector<std::uint8_t> bytes);
    [[nodiscard]] const std::vector<ShelvedBook>& books() const {
        return books_;
    }
    [[nodiscard]] const SharedPart& base() const {
        return base_part_;
    }
    // Decodes the tables and the base, if that is not done. False when either is
    // damaged; no book can then be read. read() does this itself when it must.
    [[nodiscard]] bool prepare();
    // The text of book `index`. False, with `out` cleared, when its code is damaged or
    // its text does not match its SHA-256.
    [[nodiscard]] bool read(std::size_t index, std::string& out);

  private:
    std::vector<std::uint8_t> bytes_;
    std::vector<ShelvedBook> books_;
    SharedPart tables_part_;
    SharedPart base_part_;
    enum class Prepared : std::uint8_t { no, yes, damaged };
    Prepared prepared_ = Prepared::no;
    std::unique_ptr<TextPacker> packer_;
    LzBase base_;
    [[nodiscard]] std::span<const std::uint8_t> code_of(const SharedPart& part) const;
};

struct BookText {
    std::string id;
    std::string text;
    std::string shelf;  // the shelf record, at most 65535 bytes
};

// Codes made by an earlier build, so a build codes again only what changed. A code is
// kept under a digest of everything it depends on.
class CodeCache {
  public:
    virtual ~CodeCache() = default;
    [[nodiscard]] virtual bool find(const Sha256Digest& key, std::vector<std::uint8_t>& code) = 0;
    virtual void keep(const Sha256Digest& key, const std::vector<std::uint8_t>& code) = 0;
};

struct LibraryPlan {
    // The books placed first in the base, by id, in this order. Ids not among the books
    // are ignored.
    std::vector<std::string> base;
    // Then books spread evenly through the shelf, a stride apart, while the base holds
    // under this many packed bytes: a cross-section of the library's language.
    std::size_t base_bytes = 0;
    int effort = 48;               // the coder's match search (lz_coder.hpp)
    CodeCache* cache = nullptr;
};

// The library file for `books`, in shelf order. Deterministic: the same texts and plan
// give the same bytes on every platform. False when a text cannot be packed.
[[nodiscard]] bool write_library(const std::vector<BookText>& books, const LibraryPlan& plan,
                                 std::vector<std::uint8_t>& out);

}  // namespace pl
