#include "text_requests.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using gui_forms::TextMaskStatus;
void require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}
std::vector<std::byte> read_font(const char* path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    require(static_cast<bool>(input), "Open the real provider font fixture");
    const std::streamoff length = input.tellg();
    require(length > 0 && length <= 4 * 1024 * 1024, "Bound fixture file allocation");
    std::vector<std::byte> bytes(static_cast<std::size_t>(length));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()), length);
    require(static_cast<bool>(input), "Read complete encoded font");
    return bytes;
}
struct OtherExecutor final {
    games::TextRequests& requests;
    gui_forms::TextMaskResult& result;
    void operator()() const {
        gui_forms::TextMaskRequest input{};
        input.utf8 = "wrong executor";
        gui_forms::TextMaskLease output{};
        result = requests.request(input, output);
    }
};
void collect(games::TextRequests& requests, gui_forms::TextMaskSession& session) {
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for (;;) {
        const gui_forms::TextMaskResult result = requests.poll();
        require(result.status == TextMaskStatus::success, "Poll on opening executor");
        const gui_forms::TextMaskSessionSnapshot snapshot = session.snapshot();
        if (snapshot.occupied == 0) { return; }
        require(std::chrono::steady_clock::now() < deadline, "Worker completes within test deadline");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
void exercise(gui_forms::TextMaskService& service, const gui_forms::EncodedFontLease& fonts) {
    std::unique_ptr<gui_forms::TextMaskSession> session{};
    gui_forms::TextMaskResult result = service.open_session(nullptr, session);
    require(result.status == TextMaskStatus::success, "Open real production text-mask session");
    games::TextRequests requests(*session, fonts);
    gui_forms::TextMaskLease output{};
    gui_forms::TextMaskRequest input{};
    const std::string excessive(16385, 'a');
    input.utf8 = excessive;
    result = requests.request(input, output);
    require(result.status == TextMaskStatus::limit_exceeded &&
        result.limit == gui_forms::TextMaskLimit::input_bytes && requests.outstanding() == 0,
        "Oversize input does not enter either queue");
    const std::array<std::string, 9> strings{
        "a\rb", "a\r\nb", "two", "three", "four", "five", "six", "seven", "eight"};
    input.utf8 = strings[0];
    result = requests.request(input, output);
    require(result.status == TextMaskStatus::pending, "First request enters asynchronous service");
    input.utf8 = "a\nb";
    result = requests.request(input, output);
    require(result.status == TextMaskStatus::pending && requests.outstanding() == 1,
        "Isolated CR normalizes to LF before pending deduplication");
    for (std::size_t index = 1; index < strings.size(); ++index) {
        input.utf8 = strings[index];
        result = requests.request(input, output);
        require(result.status == TextMaskStatus::pending, "Admit nine distinct requests");
    }
    require(requests.outstanding() == 9, "CRLF source bytes remain distinct from normalized LF");
    input.utf8 = "tenth";
    result = requests.request(input, output);
    require(result.status == TextMaskStatus::busy && !output.has_value(),
        "Backpressure preserves caller output at nine owned records");
    gui_forms::TextMaskResult foreign{};
    std::thread worker(OtherExecutor{requests, foreign});
    worker.join();
    require(foreign.status == TextMaskStatus::wrong_executor && requests.outstanding() == 9,
        "Wrong executor cannot inspect or mutate consumer request records");
    collect(requests, *session);
    require(requests.outstanding() == 9, "Completed results stay bounded and owned until consumed");
    for (const std::string& text : strings) {
        input.utf8 = text;
        result = requests.request(input, output);
        // Stage 1 has no raster backend. This must fail when native rendering
        // arrives, requiring real mask/metric acceptance tests before adoption.
        require(result.status == TextMaskStatus::unsupported_profile && !output.has_value(),
            "Stage-1 production failure is surfaced, never disguised as an empty mask");
    }
    require(requests.outstanding() == 0, "Consuming failures releases consumer records");
    input.utf8 = "cancelled request";
    result = requests.request(input, output);
    require(result.status == TextMaskStatus::pending, "Admit cancellation exercise");
    result = requests.cancel_all();
    require(result.status == TextMaskStatus::success && requests.outstanding() == 0,
        "Cancel drops consumer ownership without waiting for a running worker");
    (*session).begin_close();
    result = requests.request(input, output);
    require(result.status == TextMaskStatus::closing && !output.has_value(),
        "Closing a view cannot return an old completion or admit new work");
    (*session).join_and_release();
}
}
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Pass the provider's Carlito font fixture");
        const std::vector<std::byte> bytes = read_font(argv[1]);
        const std::array<gui_forms::PreparedFontSource, 1> sources{{{.encoded = bytes}}};
        gui_forms::TextMaskService service{};
        gui_forms::EncodedFontLease fonts{};
        const gui_forms::TextMaskResult registered = service.create_font_bank(sources, fonts);
        require(registered.status == TextMaskStatus::success, "Register bounded real encoded font");
        exercise(service, fonts);
        std::cout << "Consumer queue, backpressure, normalization, typed failures and close passed; raster unavailable\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
