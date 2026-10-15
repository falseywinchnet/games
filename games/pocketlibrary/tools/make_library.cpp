// Builds the library file (container.hpp) from the normalized books import_books.py
// wrote, in the catalogue's shelf order (shelf.txt), and proves it: every book is decoded again, checked against its
// SHA-256 and read as a work before the file is written.
//
//   make_library <normalized dir> <output file>
#include "../src/book.hpp"
#include "../src/container.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "make_library <normalized dir> <output file>\n");
        return 2;
    }
    const std::string folder = argv[1];
    // The shelf, in order (import_books.py writes it from the catalogue): each book is a
    // "book<TAB>id" line, its shelf record's lines, then a blank line.
    std::ifstream shelf(folder + "/shelf.txt", std::ios::binary);
    if (!shelf) {
        std::fprintf(stderr, "missing shelf.txt\n");
        return 1;
    }
    std::vector<pl::BookText> books;
    std::string line;
    while (std::getline(shelf, line)) {
        if (line.rfind("book\t", 0) == 0) {
            pl::BookText book;
            book.id = line.substr(5);
            books.push_back(book);
        } else if (!line.empty() && !books.empty()) {
            books.back().shelf += line + "\n";
        }
    }
    for (pl::BookText& book : books) {
        std::ifstream file(folder + "/" + book.id + ".tsv", std::ios::binary);
        if (!file) {
            std::fprintf(stderr, "missing %s.tsv\n", book.id.c_str());
            return 1;
        }
        book.text.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    }
    std::vector<std::uint8_t> bytes;
    pl::write_library(books, bytes);
    pl::LibraryFile library;
    if (!library.open(bytes)) {
        std::fprintf(stderr, "the file written does not open\n");
        return 1;
    }
    for (std::size_t n = 0; n < books.size(); ++n) {
        const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        std::string text;
        const bool read = library.read(n, text);
        const std::chrono::duration<double> decoded = std::chrono::steady_clock::now() - start;
        pl::Work work;
        const bool parsed = read && pl::read_work(books[n].id, text, work);
        if (!read || text != books[n].text || !parsed) {
            std::fprintf(stderr, "%s does not come back%s\n", books[n].id.c_str(), read && !parsed ? " as a work" : "");
            return 1;
        }
        std::size_t chapters = 0;
        for (const pl::Division& division : work.divisions)
            chapters += division.chapters.size();
        const pl::ShelvedBook& shelved = library.books()[n];
        std::printf("%-15s text %8u  coded %8u (%4.1f%%)  decoded in %5.1f ms  %3zu divisions, %5zu chapters\n",
                    shelved.id.c_str(), shelved.length, shelved.code_length,
                    100.0 * shelved.code_length / shelved.length, decoded.count() * 1000, work.divisions.size(),
                    chapters);
    }
    std::ofstream out(argv[2], std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    std::printf("wrote %s: %zu bytes\n", argv[2], bytes.size());
    return out.good() ? 0 : 1;
}
