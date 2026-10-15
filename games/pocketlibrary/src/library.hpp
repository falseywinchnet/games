#pragma once
// The library as the reader sees it: the books on the shelf, in shelf order, with how
// each is bound; each book read from the library file the first time it is opened and
// kept while the game lives; and the reader's place in every book. No pixels, no
// window: the scenes draw from this.
#include "book.hpp"
#include "container.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pl {

struct Binding {
    std::uint8_t red = 70, green = 64, blue = 58;  // the cloth or leather of the spine
    bool leather = false;
};

struct ShelfBook {
    std::size_t index = 0;   // in the library file
    std::string id;
    std::string title;       // gilt on the spine
    std::string subtitle;    // smaller, below it
    Binding binding;
    double heft = 1;         // how thick the volume is, 0.5 to 2.5: grows with its text
};

class Library {
  public:
    // Takes the library file's bytes. False, with an empty shelf, when the file does
    // not open.
    [[nodiscard]] bool open(std::vector<std::uint8_t> bytes);
    [[nodiscard]] const std::vector<ShelfBook>& shelf() const {
        return shelf_;
    }
    // The book, decoded and read the first time it is asked for; nullptr when its text
    // is damaged. The pointer stays valid for the life of the library.
    [[nodiscard]] const Work* work(std::string_view id);
    // The BSB's verses in order (for the concordance's references), or nullptr when the
    // library has no BSB.
    [[nodiscard]] const Versification* bible_places();

  private:
    LibraryFile file_;
    std::vector<ShelfBook> shelf_;
    std::vector<std::unique_ptr<Work>> works_;  // one per shelf book, empty until read
    std::vector<bool> failed_;
    std::unique_ptr<Versification> places_;
};

// Where the reader is in a book: its section and the left page of the open spread.
struct Place {
    std::string book;
    std::size_t section = 0;
    std::size_t page = 0;
};

struct Bookmarks {
    std::string open;  // the book open when the game was left; empty for the shelf
    int step = 1;      // the type size: 0 small, 1 medium, 2 large
    std::vector<Place> places;
};
[[nodiscard]] std::string encode_bookmarks(const Bookmarks& marks);
// False, leaving `marks` unchanged, when the text is not a bookmarks file.
[[nodiscard]] bool decode_bookmarks(std::string_view text, Bookmarks& marks);
[[nodiscard]] Place place_in(const Bookmarks& marks, std::string_view book);
void set_place(Bookmarks& marks, const Place& place);

}  // namespace pl
