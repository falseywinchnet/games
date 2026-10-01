#include "sudoku.hpp"
#include "quickjs.h"
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace games {
namespace {
std::string read_source(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("The bundled Sudoku engine or calibration is missing.");
    }
    std::ostringstream source;
    source << file.rdbuf();
    return source.str();
}

// Each background job owns its VM. Only bundled sources and explicit seed values
// enter it; no filesystem, process, network or native-module bindings are installed.
class SudokuRuntime final {
  public:
    JSRuntime* runtime = nullptr;
    JSContext* context = nullptr;

    SudokuRuntime() {
        runtime = JS_NewRuntime();
        if (runtime == nullptr) {
            throw std::runtime_error("Cannot allocate the Sudoku runtime.");
        }
        JS_SetMemoryLimit(runtime, 128 * 1024 * 1024);
        JS_SetMaxStackSize(runtime, 1024 * 1024);
        context = JS_NewContext(runtime);
        if (context == nullptr) {
            JS_FreeRuntime(runtime);
            runtime = nullptr;
            throw std::runtime_error("Cannot allocate the Sudoku context.");
        }
    }
    ~SudokuRuntime() {
        JS_FreeContext(context);
        JS_FreeRuntime(runtime);
    }
    SudokuRuntime(const SudokuRuntime&) = delete;
    SudokuRuntime& operator=(const SudokuRuntime&) = delete;

    void evaluate(const std::string& source, const char* name) const {
        JSValue result = JS_Eval(context, source.data(), source.size(), name, JS_EVAL_TYPE_GLOBAL);
        bool failed = JS_IsException(result);
        JS_FreeValue(context, result);
        if (failed) {
            JSValue exception = JS_GetException(context);
            const char* message = JS_ToCString(context, exception);
            std::string error = message != nullptr ? message : "Unknown Sudoku engine failure.";
            if (message != nullptr) {
                JS_FreeCString(context, message);
            }
            JS_FreeValue(context, exception);
            throw std::runtime_error(error);
        }
    }
};

bool read_grid(JSContext* context, JSValueConst result, const char* name,
               std::array<int, 81>& destination, int minimum) {
    JSValue array = JS_GetPropertyStr(context, result, name);
    JSValue length_value = JS_GetPropertyStr(context, array, "length");
    std::int32_t length = 0;
    bool valid = JS_IsArray(array) &&
                 JS_ToInt32(context, &length, length_value) == 0 && length == 81;
    JS_FreeValue(context, length_value);
    for (std::uint32_t i = 0; valid && i < 81; ++i) {
        JSValue element = JS_GetPropertyUint32(context, array, i);
        double value = 0;
        valid = JS_IsNumber(element) && JS_ToFloat64(context, &value, element) == 0 &&
                value >= minimum && value <= 9;
        if (valid) {
            int digit = static_cast<int>(value);
            valid = value == digit;
            destination[i] = digit;
        }
        JS_FreeValue(context, element);
    }
    JS_FreeValue(context, array);
    return valid;
}
}

Sudoku SudokuJob::operator()() const {
    if (difficulty < 0 || difficulty > 2) {
        throw std::invalid_argument("Sudoku difficulty must be easy, medium or hard.");
    }
    SudokuRuntime engine;
    engine.evaluate(read_source(asset_path + "/sudoku/engine.js"), "engine.js");
    engine.evaluate("const calibration = " + read_source(asset_path + "/sudoku/calibration.json") + ";",
                    "calibration.json");
    JSValue global = JS_GetGlobalObject(engine.context);
    int seed_status = JS_SetPropertyStr(engine.context, global, "requestedSeed",
                                        JS_NewStringLen(engine.context, seed.data(), seed.size()));
    int level_status = JS_SetPropertyStr(engine.context, global, "requestedDifficulty",
                                         JS_NewInt32(engine.context, difficulty));
    JS_FreeValue(engine.context, global);
    if (seed_status < 0 || level_status < 0) {
        throw std::runtime_error("Cannot initialize the Sudoku request.");
    }
    engine.evaluate("globalThis.generated = generate({difficulty:['easy','medium','hard']["
                    "requestedDifficulty],seed:requestedSeed,calibration});", "generate.js");
    global = JS_GetGlobalObject(engine.context);
    JSValue result = JS_GetPropertyStr(engine.context, global, "generated");
    Sudoku game;
    game.seed = seed;
    game.difficulty = difficulty;
    bool valid = read_grid(engine.context, result, "puzzle", game.puzzle, 0);
    valid = read_grid(engine.context, result, "solution", game.solution, 1) && valid;
    JS_FreeValue(engine.context, result);
    JS_FreeValue(engine.context, global);
    game.grid.values = game.puzzle;
    if (!valid || !game.invariant()) {
        throw std::runtime_error("The Sudoku engine returned an invalid puzzle.");
    }
    game.message = "Left click to set · right click to note";
    return game;
}
} // namespace games
