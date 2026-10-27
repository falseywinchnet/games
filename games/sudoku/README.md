# Sudoku / Local

A dependency-free, offline 9×9 Sudoku generator, exact uniqueness checker, logical difficulty rater, and playable standalone demo. The implementation uses the Sudoku constraint graph and one-dimensional empirical quantile transport. It creates fresh grids rather than selecting puzzles from a catalog.

## Open and play

Open **`sudoku.html`** in a modern browser. The complete engine, calibration data, worker, styles, and game are embedded in this single file. No package installation, internet connection, account, or HTTP server is required. The file's Content Security Policy explicitly blocks network connections.

The game includes a difficulty slider, easy/medium/hard presets, optional rotational clue symmetry, pencil notes, undo, error checking, logical hints, JSON export, and local progress persistence when browser storage is available. Generation runs in a Web Worker; Cancel terminates the worker and preserves the current puzzle. Some browsers restrict local-file storage. Export saves a portable puzzle, solution, and logical proof trace, but there is no import UI in this version.

The generator and bundled worker have automated runtime coverage. The UI state tests use an isolated DOM harness. Visual rendering and local-file browser behavior have **not** been manually verified: the available browser automation tool rejected `file:` navigation. Browser generation latency is also unmeasured; the published timings are Node timings on an Apple M4.

## Command line

Requires Node 20 or newer. There are no dependencies to install.

```sh
node src/cli.mjs easy
node src/cli.mjs medium my-repeatable-seed
node src/cli.mjs hard my-seed --rotational
node src/cli.mjs 0.85 my-seed --json
```

The default is medium. `--json` includes the puzzle, solution, deduction trace, calibration target, achieved percentile, and generation statistics. Empty cells are zero. The same seed and options reproduce the same puzzle for the same engine and calibration versions; elapsed time is naturally variable.

## Use the engine

```js
import { readFile } from 'node:fs/promises';
import { generate, countSolutions, analyze } from './src/engine.mjs';

const calibration = JSON.parse(
  await readFile(new URL('./src/calibration.json', import.meta.url), 'utf8')
);

const game = generate({
  difficulty: 0.8,       // [0,1], or 'easy' | 'medium' | 'hard'
  seed: 'my-game-42',    // optional; defaults to system random bytes
  calibration,
  symmetry: 'none',     // or 'rotational'
});

console.log(game.puzzle);              // 81 numbers, row-major, zero = blank
console.log(game.solution);            // its unique completion
console.log(game.rating.band);         // guaranteed requested technique band
console.log(game.rating.steps);        // complete reproducible logical proof
console.log(game.transport);           // target and actual score percentiles
console.log(countSolutions(game.puzzle)); // 1; count capped at 2
```

`generate()` is synchronous so it can be embedded in different runtimes. Run it in a worker for a responsive browser UI, as the included demo does. The source module uses only standard JavaScript and has no Node-specific imports. Named difficulty levels work without a calibration profile; numeric difficulty requires one. Without calibration, generation returns the first matching-band candidate.

### Main exports

| API | Behavior |
| --- | --- |
| `generate(options)` | Returns a unique, fully logic-solvable puzzle in the requested band, or throws. |
| `generateSolution(seed)` | Constructs a complete valid grid by randomized exact search. |
| `solveExact(board, options)` | Returns `{count, solution, nodes}`. Default count limit is 2. |
| `countSolutions(board, options)` | Returns 0, 1, or 2, where 2 means at least two. |
| `analyze(board, {trace, maxTier})` | Runs deterministic deductions with no guessing. `maxTier` is 1, 2, or 3. |
| `isValid(board, {complete})` | Checks immediate row/column/box consistency, optionally completeness. This alone does not establish solvability. |
| `parseBoard(board)`, `formatBoard(board)` | Accept/format 81 row-major digits, with dots or zeros for blanks; whitespace is allowed in strings. |
| `empiricalQuantile(sorted, p)`, `percentile(sorted, score)` | Generalized empirical inverse CDF and midrank empirical percentile. |
| `carve(seed, options)` | Enumerates logic-solvable checkpoints on a clue-removal trajectory. Used by calibration. |
| `GRAPH`, `TECHNIQUES`, `VERSION` | Frozen graph topology, technique definitions, and engine version. |

All public board inputs are copied. Invalid lengths, values, difficulty options, and budgets are rejected. Exact search node-budget exhaustion throws `SearchLimitError`; it never returns a false uniqueness result. `analyze()` reports a stall as unsolved, not as proof of impossibility. Its `valid` field means no contradiction was found by its deductions, not an independent solvability certificate.

## Why no server is needed

For standard Sudoku, validity and uniqueness are finite constraint problems that fit comfortably on a local device. Difficulty requires an explicit rating model; this implementation includes that model locally. A particular app might centralize puzzle generation, rating, catalogs, or player calibration on a server, but that is an application choice. No claim is made about why an unspecified existing app uses a server.

### 1. Graph constraints and valid generation

Model each cell as a vertex. Two vertices are adjacent when they share a row, column, or box. The graph has 81 vertices, 810 undirected edges, and degree 20. Its 27 nine-cell constraint units must each contain all nine colors. A completed Sudoku is a valid nine-coloring; a puzzle is a partial coloring with one extension.

The exact solver represents available colors as nine-bit masks. It chooses an unfilled cell with the fewest remaining colors (MRV), branches, and backtracks. Randomized ties and color order create varied completed grids. The implementation searches for completions directly, rather than only permuting one fixed base grid. This does **not** establish uniform sampling over all Sudoku solutions.

Starting from a complete grid, the generator removes clues in seeded random order. For each proposed removal it counts solutions, stopping as soon as a second completion is found. It retains a removal only when the count is exactly one. This preserves a unique completion by induction from the original complete grid. Budget-exhausted removals are rejected. A final independent production check is run on the exact board returned to the caller.

Optional symmetry removes opposite clue positions together. A 23-clue floor bounds work; this generator does not seek globally minimal puzzles or cover every possible clue count. Candidate checkpoints are graded at 46 clues or fewer.

### 2. Difficulty from logical deductions

The rating solver exhausts easier techniques before attempting harder ones. It never guesses or uses the exact solver to fill a stalled puzzle.

| Band | Logical techniques allowed | Acceptance requirement |
| --- | --- | --- |
| Easy / tier 1 | Naked and hidden singles | Entire puzzle solved with singles. |
| Medium / tier 2 | Singles and locked candidates, including pointing/claiming | Solved with locked candidates; the singles-only solver stalls. |
| Hard / tier 3 | Above, plus naked pairs, hidden pairs, naked triples, and row/column X-Wings | Solved completely; the tier-2 solver stalls. |

The trace records placements, elimination masks, and witness cells/units. It can be checked and used for hints. Deduction counts and starting candidate entropy determine a score:

```text
score = 1000 × highest technique tier
      + sum(technique weights)
      + 0.2 × initial candidate entropy
      + 0.5 × eliminated candidates

entropy = sum over empty cells of log2(candidate count)
weights: naked single 1; hidden single 2; locked candidate 12;
         naked pair 24; hidden pair 30; naked triple 36; X-Wing 48.
```

These are explicit engineering choices. They produce reproducible difficulty labels, **not** universal human difficulty ratings or estimates of solving time. An experienced player may find a different solving path. Puzzles beyond the supported logical repertoire are rejected even when uniquely solvable. Generation therefore guarantees a supported no-guess path, not that every player will recognize it.

### 3. Statistical distribution transport

The shipped local profile contains scores from 2,000 independent seeded carving trajectories, alternating ordinary and rotational clue removal: 29,572 easy, 644 medium, and 307 hard checkpoints. Checkpoints within one trajectory are correlated; these counts are not counts of independent human observations or independent complete grids.

Let `F_b` be the empirical score CDF for band `b`. For slider value `d ∈ [0,1]`, choose:

```text
b = min(2, floor(3d))
u = 3d - b
target score = F_b^(-1)(u)
```

Thus the thirds of the slider correspond to easy, medium, and hard. The inverse CDF is the generalized empirical quantile, so the transport from a uniform difficulty coordinate to the discrete score distribution is monotone. This is the one-dimensional quantile construction from optimal transport. No transport optimization server is involved.

The generator searches fresh candidates for the target score. By default, it stops when within 0.12 of the requested within-band percentile, after examining six trajectories containing matching-band candidates, or after 256 total trajectories. It then returns the closest score found **in the requested band**. Quantile endpoints and sparsely sampled tails can miss the target; the returned `quantileError` and `withinTolerance` disclose this. Finite search approximates the requested transport: generated samples are not claimed to exactly follow a target distribution, nor is each individual slider increase guaranteed to increase experienced difficulty.

Increase `candidateBudget` and `maxAttempts`, or lower `tolerance`, to spend more computation on score matching. The maximum supported budgets are 10,000. If no candidate meets the requested band, `GenerationError` is thrown; difficulty is never silently downgraded. The generator does not reuse a precomputed puzzle on failure.

To calibrate to actual players later, collect local solve-time/error data and validate a new score model against it. The current shipped model uses synthetic solver measurements only.

## Validation and performance

`reports/benchmark.json` contains the measured results and hardware/runtime identification. `reports/transport.json` measures slider targeting separately. Generation timing includes grid construction, all carving uniqueness checks, logical grading, quantile targeting, and the final production uniqueness check. The separate independent oracle and trace replay are outside the timed region.

Measured on the user's **Apple M4 Mini, Node v25.2.1**, with 1,000 held-out puzzles per band and both symmetry modes:

| Difficulty | Median | 95th percentile | Slowest |
| --- | ---: | ---: | ---: |
| Easy | 0.31 ms | 0.40 ms | 4.37 ms |
| Medium | 21.65 ms | 84.69 ms | 195.72 ms |
| Hard | 74.11 ms | 226.80 ms | 468.57 ms |

All 3,000 puzzles passed independent uniqueness and solution checks. All 155,487 logical trace steps passed independent replay, exercising all seven supported techniques. There were no generation failures or duplicate puzzles in this batch. Timing follows a 12-puzzle JIT warm-up; other hardware, cold starts, browsers, and extreme percentile requests can differ. This is measured performance, not a worst-case deadline guarantee.

All requested bands were satisfied. The exact within-band percentile tolerance was reached for 100% of easy, 94% of medium, and 79.6% of hard requests in this benchmark; other requests returned the nearest score found in the correct band, with the miss reported in metadata.

The independent oracle implements Algorithm X with sets over 324 exact-cover constraints. It imports no production engine code, masks, or peer graph. Every puzzle in the stress benchmark must:

- Have a valid complete solution matching every given clue.
- Have exactly one solution according to independent Algorithm X, matching the engine's solution.
- Have a complete deduction trace whose witnesses and eliminations pass independent replay.
- Belong to its requested difficulty band; medium/hard must stall at the lower tier.
- Be distinct from every other puzzle in that benchmark batch.

Additional tests cover malformed input, inconsistent/unsatisfiable boards, multiple solutions, random partial-grid differential testing, deterministic seeds, symmetric clues, search-budget exhaustion, honest generation failure, quantile ties, embedded worker execution, and application state transitions.

```sh
npm test                       # automated correctness and app-state tests
npm run build                  # rebuild standalone sudoku.html
npm run calibrate              # regenerate 2,000-trajectory profile
node scripts/benchmark.mjs 1000 # 1,000 puzzles per band, 3,000 total
node scripts/transport-check.mjs # 600 held-out slider requests
```

For CPU-heavy runs on this user's M4 Mini, use the repository `AGENTS.md` commands. Local files remain authoritative. Rebuild the demo after engine, app, template, or calibration changes. Recalibrate and version the model when scoring or technique semantics change.

## Mathematical references

- [NetworkX: Sudoku and graph coloring](https://networkx.org/nx-guides/content/generators/sudoku.html) describes the 81-vertex, 810-edge graph and coloring formulation.
- [Peyré and Cuturi, Computational Optimal Transport](https://arxiv.org/abs/1803.00567) develops optimal transport, including the one-dimensional quantile formulation.
- [Knuth, Dancing Links](https://arxiv.org/abs/cs/0011047) describes Algorithm X and efficient exact-cover search. The independent test oracle here uses straightforward sets rather than dancing links.

This project combines established algorithms in a new implementation. It makes no claim of a new graph-theory theorem, globally uniform Sudoku sampling, universally accepted difficulty labels, or a hard wall-clock generation deadline.
