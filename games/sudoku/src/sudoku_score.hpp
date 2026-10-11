#pragma once
// Sudoku's music as a live score: the koto garden (sudoku_music.hpp), by day or by night.
// Its sound effects stay the recorded ones.
#include "audio.hpp"

#include <memory>

namespace ps_sudoku {
// Null when the toolkit cannot play live sources (the recorded loops play instead).
[[nodiscard]] std::shared_ptr<games::LiveScore> make_score(bool night);
}  // namespace ps_sudoku
