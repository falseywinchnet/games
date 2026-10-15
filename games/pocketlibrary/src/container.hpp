#pragma once
// The library file: one read-only file holding every book, each coded on its own so
// a book opens without the others, and each checked by the SHA-256 of its text when
// it is decoded. Little-endian throughout:
//
//   "PLIB", version (u32, 1), book count (u32), coder (u32, 1 = lz_coder)
//   per book: id length (u8), id, text length (u32), code offset (u32),
//             code length (u32), SHA-256 of the text (32 bytes)
//   the codes, in the same order
//
// A book's text is its normalized records (book.hpp reads them).
#include "sha256.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace pl {

struct ShelvedBook {
    std::string id;
    std::uint32_t length = 0;
    std::uint32_t offset = 0;
    std::uint32_t code_length = 0;
    Sha256Digest digest{};
};

class LibraryFile {
  public:
    // Takes the file's bytes. False, holding nothing, when the header or the table is
    // damaged or a code lies outside the file.
    [[nodiscard]] bool open(std::vector<std::uint8_t> bytes);
    [[nodiscard]] const std::vector<ShelvedBook>& books() const {
        return books_;
    }
    // The text of book `index`. False, with `out` cleared, when its code is damaged or
    // its text does not match its SHA-256.
    [[nodiscard]] bool read(std::size_t index, std::string& out) const;

  private:
    std::vector<std::uint8_t> bytes_;
    std::vector<ShelvedBook> books_;
};

struct BookText {
    std::string id;
    std::string text;
};
// The library file for `books`, in order. Deterministic: the same texts give the same
// bytes on every platform.
void write_library(const std::vector<BookText>& books, std::vector<std::uint8_t>& out);

}  // namespace pl
