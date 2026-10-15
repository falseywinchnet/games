// The library without pixels: the record reader, the coder, the library file and its
// checks, the BSB's versification and the bookmarks. No window, no fonts.
#include "book.hpp"
#include "container.hpp"
#include "library.hpp"
#include "lz_coder.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        ++failures;
    }
}

std::span<const std::uint8_t> bytes_of(const std::string& text) {
    return std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
}

// Two books of a small Bible, in the normalized form import_books.py writes.
const std::string sample_bible =
    "H\tsource\ttest\n"
    "B\tGEN\tGenesis\tGenesis\tGen\n"
    "M\t2\tGenesis\n"
    "C\t1\n"
    "S\t1\tThe Creation\n"
    "P\tp\n"
    "V\t1\n"
    "T\tIn the beginning God created the heavens and the earth.\n"
    "V\t2\n"
    "T\tNow the earth was formless and void.\n"
    "N\t1:2\n"
    "T\tOr \n"
    "Y\titalic\t1\n"
    "T\twaste\n"
    "Y\titalic\t0\n"
    "E\n"
    "C\t2\n"
    "P\tq1\n"
    "V\t1\n"
    "T\tThus the heavens were completed.\n"
    "B\tMRK\tThe Gospel According to Mark\tMark\tMark\n"
    "C\t1\n"
    "P\tp\n"
    "V\t1\n"
    "Y\twj\t1\n"
    "T\t\xe2\x80\x9c" "Follow Me,\xe2\x80\x9d\n"
    "Y\twj\t0\n"
    "T\the said.\n";

void test_records() {
    pl::Work work;
    check(pl::read_work("bsb", sample_bible, work), "the sample Bible reads");
    check(work.divisions.size() == 2, "two divisions");
    check(work.divisions[0].abbreviation == "Gen" && work.divisions[1].short_title == "Mark", "titles and abbreviations");
    const pl::Chapter& first = work.divisions[0].chapters[0];
    check(first.label == "1" && first.blocks.size() == 2, "a heading and a paragraph in Genesis 1");
    check(first.notes.size() == 1 && first.notes[0].label == "1:2", "the footnote is kept with its reference");
    check(first.notes[0].inlines.size() == 2 && (first.notes[0].inlines[1].flags & pl::span_italic) != 0,
          "the footnote's italic span");
    const pl::Block& paragraph = first.blocks[1];
    int verses = 0, calls = 0;
    for (const pl::Inline& item : paragraph.inlines) {
        verses += item.kind == pl::InlineKind::verse ? 1 : 0;
        calls += item.kind == pl::InlineKind::note_call ? 1 : 0;
    }
    check(verses == 2 && calls == 1, "two verses and one footnote call");
    const pl::Block& words = work.divisions[1].chapters[0].blocks[0];
    check(words.inlines.size() == 3 && (words.inlines[1].flags & pl::span_red) != 0 &&
              (words.inlines[2].flags & pl::span_red) == 0,
          "the words of Christ open and close");
    pl::Work broken;
    check(!pl::read_work("x", "Z\tnothing\n", broken) && broken.divisions.empty(), "an unknown record is refused");
    check(!pl::read_work("x", "E\n", broken), "a footnote closed without opening is refused");

    pl::Versification places;
    pl::versify(work, places);
    check(places.places.size() == 4, "four verses");
    check(places.places[2].chapter == 1 && places.places[2].verse == "1", "the third verse is Genesis 2:1");
    check(places.places[3].division == 1, "the fourth is in Mark");
}

void test_coder() {
    std::string text;
    for (int n = 0; n < 4000; ++n)
        text += "And there was evening, and there was morning: day " + std::to_string(n % 37) + ".\n";
    std::vector<std::uint8_t> code;
    pl::lz_encode(bytes_of(text), code);
    std::vector<std::uint8_t> back;
    check(pl::lz_decode(code, text.size(), back), "the code decodes");
    check(std::string(back.begin(), back.end()) == text, "every byte comes back");
    check(code.size() * 20 < text.size(), "a repetitive text codes to under a twentieth");
    std::vector<std::uint8_t> nothing;
    std::vector<std::uint8_t> empty_code;
    pl::lz_encode(nothing, empty_code);
    check(pl::lz_decode(empty_code, 0, back) && back.empty(), "an empty text");
    check(!pl::lz_decode(std::span<const std::uint8_t>(code.data(), code.size() / 3), text.size(), back) ||
              std::string(back.begin(), back.end()) != text,
          "a code cut short does not give the text back");
}

void test_library_file() {
    std::vector<pl::BookText> books(2);
    books[0].id = "bsb";
    books[0].text = sample_bible;
    books[0].shelf = "title\tHOLY BIBLE\nsubtitle\tBerean Standard Bible\ncategory\tScripture\nbinding\t1b1715\nleather\t1\n";
    books[1].id = "notes";
    books[1].text = "B\tN\tNotes\tNotes\nC\t\nP\tp\nT\tA line.\n";
    std::vector<std::uint8_t> bytes;
    pl::write_library(books, bytes);
    std::vector<std::uint8_t> again;
    pl::write_library(books, again);
    check(bytes == again, "the same books give the same file");
    pl::Library library;
    check(library.open(bytes), "the library opens");
    check(library.shelf().size() == 2 && library.shelf()[0].title == "HOLY BIBLE", "the shelf, from the shelf record");
    check(library.shelf()[0].category == "Scripture" && library.shelf()[0].binding.leather &&
              library.shelf()[0].binding.red == 0x1b,
          "the category and binding");
    check(library.shelf()[1].title == "notes", "a book without a shelf record is lettered with its id");
    const pl::Work* bible = library.work("bsb");
    check(bible != nullptr && (*bible).divisions.size() == 2, "the Bible is read from the file");
    check(library.work("bsb") == bible, "and read once");
    const pl::Versification* places = library.bible_places();
    check(places != nullptr && (*places).places.size() == 4, "the Bible's verses for the concordance");

    // A damaged byte inside a code is caught by the code or by the text's SHA-256.
    pl::LibraryFile file;
    check(file.open(bytes), "the file opens as a file");
    const pl::ShelvedBook first = file.books()[0];
    std::vector<std::uint8_t> damaged = bytes;
    damaged[first.offset + first.code_length / 2] ^= 0x10U;
    check(file.open(damaged), "the damaged file's table still opens");
    std::string text;
    check(!file.read(0, text) && text.empty(), "the damaged book is refused");
    check(file.read(1, text) && text == books[1].text, "the undamaged book still reads");
    std::vector<std::uint8_t> short_file(bytes.begin(), bytes.begin() + 20);
    check(!file.open(short_file), "a file cut short is refused");
}

void test_bookmarks() {
    pl::Bookmarks marks;
    marks.open = "avot";
    marks.step = 2;
    pl::Place place;
    place.book = "bsb";
    place.section = 40;
    place.page = 6;
    pl::set_place(marks, place);
    place.page = 8;
    pl::set_place(marks, place);
    check(marks.places.size() == 1, "a book has one place");
    pl::Bookmarks read;
    check(pl::decode_bookmarks(pl::encode_bookmarks(marks), read), "the bookmarks read back");
    check(read.open == "avot" && read.step == 2, "the open book and the type size");
    const pl::Place back = pl::place_in(read, "bsb");
    check(back.section == 40 && back.page == 8, "the place in the Bible");
    check(pl::place_in(read, "jps1917").section == 0, "a book never opened opens at its start");
    check(!pl::decode_bookmarks("something else", read), "another file is refused");
}

}  // namespace

int main() {
    test_records();
    test_coder();
    test_library_file();
    test_bookmarks();
    if (failures == 0)
        std::printf("pocketlibrary library tests passed\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
