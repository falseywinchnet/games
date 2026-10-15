// Builds the library file (container.hpp) from the normalized books import_books.py
// wrote, in the catalogue's shelf order (shelf.txt), and proves it: every book is
// decoded again, checked against its SHA-256 and read as a work before the file is
// written.
//
//   make_library [options] <output file> <normalized dir> [more dirs...]
//     --cache <dir>     keep codes there and code again only what changed
//     --base <id,id>    the books placed first in the base (default bsb,jps1917)
//     --base-mb <n>     then fill the base to n megabytes of packed text (default 16)
//     --effort <n>      the coder's match search (default 48)
//     --quiet           no line per book
//     --trial           a book that does not read as a work is reported, not fatal
//                       (for measuring trial imports; never for the shipped file)
//
// A folder with a shelf.txt gives its books in that order with their shelf records; a
// folder without one (a trial import) gives every .tsv in name order, unshelved.
#include "../src/book.hpp"
#include "../src/container.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

class FolderCache final : public pl::CodeCache {
  public:
    explicit FolderCache(std::string folder) : folder_(std::move(folder)) {
        std::filesystem::create_directories(folder_);
    }
    bool find(const pl::Sha256Digest& key, std::vector<std::uint8_t>& code) override {
        std::ifstream kept(path(key), std::ios::binary);
        if (!kept)
            return false;
        code.assign((std::istreambuf_iterator<char>(kept)), std::istreambuf_iterator<char>());
        ++found;
        return true;
    }
    void keep(const pl::Sha256Digest& key, const std::vector<std::uint8_t>& code) override {
        std::ofstream fresh(path(key), std::ios::binary);
        fresh.write(reinterpret_cast<const char*>(code.data()), static_cast<std::streamsize>(code.size()));
        ++made;
    }
    std::size_t found = 0, made = 0;

  private:
    std::string folder_;
    std::string path(const pl::Sha256Digest& key) const {
        return folder_ + "/" + pl::sha256_hex(key) + ".lz";
    }
};

bool read_file(const std::string& path, std::string& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return false;
    out.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return true;
}

bool read_folder(const std::string& folder, std::vector<pl::BookText>& books) {
    std::ifstream shelf(folder + "/shelf.txt", std::ios::binary);
    const std::size_t first = books.size();
    if (shelf) {
        // Each book is a "book<TAB>id" line, its shelf record's lines, then a blank line.
        std::string line;
        while (std::getline(shelf, line)) {
            if (line.rfind("book\t", 0) == 0) {
                pl::BookText book;
                book.id = line.substr(5);
                books.push_back(book);
            } else if (!line.empty() && books.size() > first) {
                books.back().shelf += line + "\n";
            }
        }
    } else {
        std::vector<std::string> ids;
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(folder))
            if (entry.path().extension() == ".tsv")
                ids.push_back(entry.path().stem().string());
        std::sort(ids.begin(), ids.end());
        for (const std::string& id : ids) {
            pl::BookText book;
            book.id = id;
            books.push_back(book);
        }
    }
    for (std::size_t n = first; n < books.size(); ++n) {
        if (!read_file(folder + "/" + books[n].id + ".tsv", books[n].text)) {
            std::fprintf(stderr, "missing %s.tsv\n", books[n].id.c_str());
            return false;
        }
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    pl::LibraryPlan plan;
    plan.base = {"bsb", "jps1917"};
    plan.base_bytes = 16'000'000;
    std::string cache_folder;
    bool quiet = false, trial = false;
    std::vector<std::string> paths;
    for (int a = 1; a < argc; ++a) {
        const std::string arg = argv[a];
        if (arg == "--cache" && a + 1 < argc) {
            cache_folder = argv[++a];
        } else if (arg == "--base" && a + 1 < argc) {
            plan.base.clear();
            const std::string list = argv[++a];
            std::size_t at = 0;
            while (at < list.size()) {
                const std::size_t comma = std::min(list.find(',', at), list.size());
                if (comma > at)
                    plan.base.push_back(list.substr(at, comma - at));
                at = comma + 1;
            }
        } else if (arg == "--base-mb" && a + 1 < argc) {
            plan.base_bytes = static_cast<std::size_t>(std::stod(argv[++a]) * 1e6);
        } else if (arg == "--effort" && a + 1 < argc) {
            plan.effort = std::stoi(argv[++a]);
        } else if (arg == "--quiet") {
            quiet = true;
        } else if (arg == "--trial") {
            trial = true;
        } else {
            paths.push_back(arg);
        }
    }
    if (paths.size() < 2) {
        std::fprintf(stderr, "make_library [--cache dir] [--base ids] [--base-mb n] [--effort n] [--quiet] "
                             "<output file> <normalized dir> [more dirs...]\n");
        return 2;
    }
    std::vector<pl::BookText> books;
    for (std::size_t p = 1; p < paths.size(); ++p)
        if (!read_folder(paths[p], books))
            return 1;
    std::unique_ptr<FolderCache> cache;
    if (!cache_folder.empty()) {
        cache = std::make_unique<FolderCache>(cache_folder);
        plan.cache = cache.get();
    }
    std::vector<std::uint8_t> bytes;
    const std::chrono::steady_clock::time_point coding = std::chrono::steady_clock::now();
    if (!pl::write_library(books, plan, bytes)) {
        std::fprintf(stderr, "a book does not pack\n");
        return 1;
    }
    const std::chrono::duration<double> coded = std::chrono::steady_clock::now() - coding;
    if (cache)
        std::printf("coded in %.1f s; codes: %zu made, %zu from the cache\n", coded.count(), (*cache).made,
                    (*cache).found);
    else
        std::printf("coded in %.1f s\n", coded.count());

    pl::LibraryFile library;
    if (!library.open(bytes)) {
        std::fprintf(stderr, "the file written does not open\n");
        return 1;
    }
    const std::chrono::steady_clock::time_point preparing = std::chrono::steady_clock::now();
    if (!library.prepare()) {
        std::fprintf(stderr, "the tables or the base do not decode\n");
        return 1;
    }
    const std::chrono::duration<double> prepared = std::chrono::steady_clock::now() - preparing;
    std::size_t text_total = 0, packed_total = 0, in_base = 0, coded_total = 0;
    double decode_total = 0;
    for (std::size_t n = 0; n < books.size(); ++n) {
        const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        std::string text;
        const bool read = library.read(n, text);
        const std::chrono::duration<double> decoded = std::chrono::steady_clock::now() - start;
        decode_total += decoded.count();
        pl::Work work;
        const bool parsed = read && pl::read_work(books[n].id, text, work);
        if (!read || text != books[n].text) {
            std::fprintf(stderr, "%s does not come back\n", books[n].id.c_str());
            return 1;
        }
        if (!parsed) {
            std::fprintf(stderr, "%s does not read as a work\n", books[n].id.c_str());
            if (!trial)
                return 1;
        }
        const pl::ShelvedBook& shelved = library.books()[n];
        text_total += shelved.length;
        packed_total += shelved.packed_length;
        coded_total += shelved.code_length;
        in_base += shelved.in_base ? shelved.packed_length : 0;
        if (quiet)
            continue;
        std::size_t chapters = 0;
        for (const pl::Division& division : work.divisions)
            chapters += division.chapters.size();
        if (shelved.in_base)
            std::printf("%-36s text %9u  packed %9u  in the base         read in %6.1f ms  %4zu chapters\n",
                        shelved.id.c_str(), shelved.length, shelved.packed_length, decoded.count() * 1000, chapters);
        else
            std::printf("%-36s text %9u  packed %9u  coded %9u (%4.1f%%)  read in %6.1f ms  %4zu chapters\n",
                        shelved.id.c_str(), shelved.length, shelved.packed_length, shelved.code_length,
                        100.0 * shelved.code_length / shelved.length, decoded.count() * 1000, chapters);
    }
    const pl::SharedPart& base = library.base();
    std::printf("%zu books: text %zu, packed %zu; base %u packed (%zu of books) coded %u, prepared in %.0f ms; "
                "other books coded %zu; read all in %.2f s\n",
                books.size(), text_total, packed_total, base.length, in_base, base.code_length,
                prepared.count() * 1000, coded_total, decode_total);
    std::ofstream out(paths[0], std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    std::printf("wrote %s: %zu bytes (%.2f%% of the text)\n", paths[0].c_str(), bytes.size(),
                100.0 * static_cast<double>(bytes.size()) / static_cast<double>(text_total));
    return out.good() ? 0 : 1;
}
