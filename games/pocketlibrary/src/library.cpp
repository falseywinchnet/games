#include "library.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>

namespace pl {
namespace {

// The cloths a book without a binding of its own is bound in, chosen by its id so a
// book keeps its colour.
constexpr Binding cloths[] = {
    {31, 42, 68, false}, {47, 74, 51, false}, {90, 31, 31, false}, {74, 52, 36, false}, {58, 40, 70, false},
    {28, 60, 66, false}, {96, 72, 38, false}, {50, 50, 54, false}, {82, 46, 30, false}, {38, 56, 40, false},
};

std::uint8_t hex_byte(std::string_view text, std::size_t at) {
    std::uint32_t value = 0;
    for (std::size_t i = at; i < at + 2 && i < text.size(); ++i) {
        const char c = text[i];
        value <<= 4;
        if (c >= '0' && c <= '9')
            value |= static_cast<std::uint32_t>(c - '0');
        else if (c >= 'a' && c <= 'f')
            value |= static_cast<std::uint32_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            value |= static_cast<std::uint32_t>(c - 'A' + 10);
    }
    return static_cast<std::uint8_t>(value);
}

// Reads a shelf record: lines of key TAB value.
void read_shelf_record(std::string_view record, ShelfBook& book) {
    std::size_t start = 0;
    while (start < record.size()) {
        std::size_t end = record.find('\n', start);
        if (end == std::string_view::npos)
            end = record.size();
        const std::string_view line = record.substr(start, end - start);
        start = end + 1;
        const std::size_t tab = line.find('\t');
        if (tab == std::string_view::npos)
            continue;
        const std::string_view key = line.substr(0, tab);
        const std::string_view value = line.substr(tab + 1);
        if (key == "title")
            book.title = std::string(value);
        else if (key == "subtitle")
            book.subtitle = std::string(value);
        else if (key == "category")
            book.category = std::string(value);
        else if (key == "binding" && value.size() == 6) {
            book.binding.red = hex_byte(value, 0);
            book.binding.green = hex_byte(value, 2);
            book.binding.blue = hex_byte(value, 4);
        } else if (key == "leather")
            book.binding.leather = value == "1";
    }
}

void parse_place(std::string_view rest, Place& place) {
    const std::size_t first = rest.find(' ');
    const std::size_t second = first == std::string_view::npos ? first : rest.find(' ', first + 1);
    if (second == std::string_view::npos)
        return;
    place.book = std::string(rest.substr(0, first));
    const std::string_view section = rest.substr(first + 1, second - first - 1);
    const std::string_view page = rest.substr(second + 1);
    std::from_chars(section.data(), section.data() + section.size(), place.section);
    std::from_chars(page.data(), page.data() + page.size(), place.page);
}

}  // namespace

bool Library::open(std::vector<std::uint8_t> bytes) {
    shelf_.clear();
    works_.clear();
    failed_.clear();
    places_.reset();
    if (!file_.open(std::move(bytes)))
        return false;
    for (std::size_t n = 0; n < file_.books().size(); ++n) {
        const ShelvedBook& stored = file_.books()[n];
        ShelfBook book;
        book.index = n;
        book.id = stored.id;
        book.title = stored.id;
        std::uint32_t hash = 2166136261U;
        for (const char c : stored.id)
            hash = (hash ^ static_cast<std::uint8_t>(c)) * 16777619U;
        book.binding = cloths[hash % (sizeof(cloths) / sizeof(cloths[0]))];
        read_shelf_record(stored.shelf, book);
        // A volume's thickness follows the square root of its text: the Bible is
        // about twice a small treatise, not twenty times.
        const double megabytes = static_cast<double>(stored.length) / 1.0e6;
        book.heft = std::clamp(0.5 + std::sqrt(megabytes) * 0.9, 0.5, 2.5);
        shelf_.push_back(book);
    }
    works_.resize(shelf_.size());
    failed_.assign(shelf_.size(), false);
    return true;
}

void Library::prepare() {
    static_cast<void>(file_.prepare());
}

const Work* Library::work(std::string_view id) {
    for (std::size_t n = 0; n < shelf_.size(); ++n) {
        if (shelf_[n].id != id)
            continue;
        if (works_[n])
            return works_[n].get();
        if (failed_[n])
            return nullptr;
        std::string text;
        std::unique_ptr<Work> made = std::make_unique<Work>();
        if (!file_.read(shelf_[n].index, text) || !read_work(shelf_[n].id, text, *made)) {
            failed_[n] = true;
            return nullptr;
        }
        works_[n] = std::move(made);
        return works_[n].get();
    }
    return nullptr;
}

const Versification* Library::bible_places() {
    if (places_)
        return places_.get();
    const Work* bible = work("bsb");
    if (bible == nullptr)
        return nullptr;
    places_ = std::make_unique<Versification>();
    versify(*bible, *places_);
    return places_.get();
}

std::string encode_bookmarks(const Bookmarks& marks) {
    std::string out = "pocket-library-bookmarks 1\n";
    out += "open " + marks.open + "\n";
    out += "type " + std::to_string(marks.step) + "\n";
    for (const Place& place : marks.places)
        out += "at " + place.book + " " + std::to_string(place.section) + " " + std::to_string(place.page) + "\n";
    return out;
}

bool decode_bookmarks(std::string_view text, Bookmarks& marks) {
    const std::string_view header = "pocket-library-bookmarks 1\n";
    if (text.substr(0, header.size()) != header)
        return false;
    Bookmarks made;
    std::size_t start = header.size();
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos)
            end = text.size();
        const std::string_view line = text.substr(start, end - start);
        start = end + 1;
        if (line.substr(0, 5) == "open ") {
            made.open = std::string(line.substr(5));
        } else if (line.substr(0, 5) == "type ") {
            const std::string_view value = line.substr(5);
            int step = 1;
            std::from_chars(value.data(), value.data() + value.size(), step);
            made.step = std::clamp(step, 0, 2);
        } else if (line.substr(0, 3) == "at ") {
            Place place;
            parse_place(line.substr(3), place);
            if (!place.book.empty())
                made.places.push_back(place);
        }
    }
    marks = std::move(made);
    return true;
}

Place place_in(const Bookmarks& marks, std::string_view book) {
    for (const Place& place : marks.places)
        if (place.book == book)
            return place;
    Place fresh;
    fresh.book = std::string(book);
    return fresh;
}

void set_place(Bookmarks& marks, const Place& place) {
    for (Place& known : marks.places) {
        if (known.book == place.book) {
            known = place;
            return;
        }
    }
    marks.places.push_back(place);
}

}  // namespace pl
