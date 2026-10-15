// Times the reading of every book of a library file: decoding and unpacking, the
// SHA-256 check alone, and reading the text as a work. Not shipped.
//
//   read_lab <library file> [repeats]
#include "../src/book.hpp"
#include "../src/container.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

double seconds_since(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2)
        return 2;
    const int repeats = argc > 2 ? std::stoi(argv[2]) : 1;
    std::ifstream file(argv[1], std::ios::binary);
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    pl::LibraryFile library;
    if (!library.open(bytes))
        return 1;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    if (!library.prepare())
        return 1;
    const double prepare = seconds_since(start);
    double read = 0, digest = 0, parse = 0, base_read = 0;
    std::size_t text = 0, base_text = 0, unparsed = 0;
    for (int r = 0; r < repeats; ++r) {
        for (std::size_t n = 0; n < library.books().size(); ++n) {
            std::string out;
            start = std::chrono::steady_clock::now();
            if (!library.read(n, out))
                return 1;
            const double took = seconds_since(start);
            read += took;
            text += out.size();
            if (library.books()[n].in_base) {
                base_read += took;
                base_text += out.size();
            }
            start = std::chrono::steady_clock::now();
            const pl::Sha256Digest d = pl::sha256(
                std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(out.data()), out.size()));
            digest += seconds_since(start) + (d[0] == 1 ? 1e-12 : 0);
            pl::Work work;
            start = std::chrono::steady_clock::now();
            const bool parsed = pl::read_work(library.books()[n].id, out, work);
            parse += seconds_since(start);
            unparsed += parsed ? 0 : 1;
        }
    }
    std::printf("prepared in %.0f ms. read %.0f MB of text at %.0f MB/s (books in the base %.0f MB/s, others %.0f MB/s); "
                "of which SHA-256 at %.0f MB/s; read as works at %.0f MB/s (%zu do not read)\n",
                prepare * 1000, text / 1e6, text / read / 1e6, base_text / base_read / 1e6,
                (text - base_text) / (read - base_read) / 1e6, text / digest / 1e6, text / parse / 1e6, unparsed);
    return 0;
}
