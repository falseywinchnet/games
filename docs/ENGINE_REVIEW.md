# Engine review: Stillwater and Rock Stack

Reviewed on 2026-10-10 from `eff7d9b2c773530006e9d93baeea92af06d29899`,
on `codex/engine-review`. The brief is `.codex-brief.md`. This review preserves
the collection, saves, animation, resolution, filtering and lighting. No toolkit
checkout, other worktree, branch, existing test or test expectation was changed.

## Result and ranking

**Implemented:** use the existing serial renderer for Rock Stack's quiet frames
when the scenery/camera cache is valid. This removes worker dispatch, atomic band
claims, repeated queue traversal and repeated triangle setup across bands. It
does not freeze the brook, crane, operator or their shadows. Interaction and
scenery rebuilds retain the threaded path. The misleading “only the brook is
redrawn” comments in `site.hpp` now describe the actual behavior.

The only measured, implemented saving is finding 1. The remaining entries are
ranked by the measured cost they address, **not by an invented expected saving**.
Their costs overlap; do not add the percentages or treat an entire stage as
removable. The correctness finding has a separate priority from performance.

Implementation and reproducible review tools: commit `cd1ec99`
(`Avoid worker dispatch for quiet Rock Stack frames`). No other production
optimization or correctness change is included in this review.

| Rank | Finding | Kind | Measured opportunity / saving | Status |
| --- | --- | --- | --- | --- |
| 1 | Quiet Rock Stack frames need not dispatch seven workers | Done inefficiently / unnecessary work | Final same-build comparison: 2.88–5.88 ms less process CPU per frame (34–39%); native median busy samples down 36.2% | Implemented; exact color/depth comparisons |
| 2 | Rock Stack re-filters every scenery receiver against largely unchanged shadows | Unnecessary work | `shadow_band`: 452 of 1,625 busy samples in representative native run; 27.8% | Proposed; no saving claimed |
| 3 | Stationary bowl, rocks and crane sections repeat projection, setup and shading | Done inefficiently / unnecessary work | `Renderer::draw`: 352 samples; textured shadowed fill: 324; untextured shadowed fill: 159 | Proposed; costs include moving content |
| 4 | Planted-tank foliage remains the dominant ambient update | Unnecessary work / inefficient traversal | 10.72 ms per sway update at 590×380; Gouraud scan median 191 native samples | Proposed exact culling/visibility work; no cadence reduction |
| 5 | Ambient shades actor fragments that later opaque fragments overwrite | Unnecessary work | Actor rasterization + preparation/shading dominate reef; compose 1.68 ms/frame at 590×380 | Proposed; overwritten subset not yet isolated |
| 6 | Fixed-layer triangle constants are recomputed per sample | Done inefficiently | Fixed builds at 590×380: 64.1 / 89.1 / 81.3 ms for planted / reef / pool | Proposed; these are whole-build costs |
| 7 | Presentation repeats row expansion / complete-frame publication | Done inefficiently / unnecessary work | Native Rock Stack `present_scene`: 37 samples; planted `present_nearest`: median 31 | Lower priority; no native damage rewrite |
| — | One-surviving-vertex near clipping uses the wrong attribute weights | Done wrong | 63/63 covered diagnostic pixels have wrong attributes; max error 0.322592 | Confirmed, documented, not folded into the performance change |

## Measurement conditions and limits

All compilation and performance measurements ran on the selected M4 Mini via
`m4build`/`m4host`: Mac16,10, 10 logical CPUs, 16 GiB, macOS 26.5 (25F71).
Compiler: Clang 22.1.8, Release, two compile jobs, the supplied dogfood toolchain
and its macOS-14 LLVM runtime. The complete application was built with
`.dog-build.sh dogfood all`, plus `rockstack_site_preview`. The first invocation
needed `mkdir -p .build` because the supplied helper redirects its configure log
there before creating it. No change to the helper was needed.

The mirror is
`/Users/joshuahkuttenkuler/Developer/CodexBuilds/playsuite-games-codex-030dc34dcf6e`.
The baseline application is preserved there as
`.build/app-dogfood/games-before.app`. Raw logs, samples, isolated saves and
images are in ignored `astra/engine-review/`, copied back to this worktree.
The `m4build` mirror deletes destination-only files outside its excluded build
directories: copy new evidence back before another synchronization.

Native probes run one application at a time with fresh `GAMES_STATE_DIR`, a
1100×720 window, ordinary motion/audio preferences, a six-second `/usr/bin/sample`
starting 16 seconds after launch, and a capture at 24 seconds **after sampling**.
Each game/tank has three runs. `GAMES_SCENE_TRACE=1` records Stillwater's actual
rates, timing and lease misses. Captures were inspected to verify that the site
and the intended planted, reef and pool tanks loaded. The supplied fresh-site
Rock Stack procedure was reproduced first; the final application comparison
uses copies of the same isolated save snapshot.

“Busy samples” uses the supplied probe's top-of-stack summation, excluding wait
functions. It is a diagnostic sample count, not a CPU percentage or elapsed
milliseconds. The Mini is shared; unrelated workloads were not stopped. Native
startup-to-exit draw counters include startup, HUD and the final capture, so
they are not a six-second animation frame count. No build or other benchmark of
this review ran during a sampling interval.

Headless timings use deterministic inputs, three repeats, medians, and process
CPU (`getrusage`, user + system) as well as elapsed time for Rock Stack. They
exclude native presentation. Unpaced headless loops have substantially different
timing from a governed native window; extrapolated “ms per second at 24 fps” is
not a measured native CPU percentage.

Baseline native observations (each entry is the three runs, not an average of
different tanks):

| Scene | Busy samples / 6 s | Native draws / whole run | Traced draw ms/frame | Traced present ms/frame |
| --- | --- | --- | --- | --- |
| Rock Stack, fresh sites | 1625 / 1040 / 1682 | 257 / 278 / 272 | — | — |
| Planted | 752 / 746 / 747 | 319 / 318 / 319 | 6.08 / 6.28 / 6.13 | 0.62 / 0.68 / 0.66 |
| Reef | 555 / 734 / 763 | 332 / 340 / 318 | 7.08 / 7.69 / 9.25 | 0.72 / 0.71 / 0.88 |
| Pool | 795 / 810 / 669 | 320 / 319 / 394 | 8.78 / 8.80 / 5.78 | 0.89 / 0.87 / 0.59 |

Every tank ended at the existing 12 fps / 3 sway-updates-per-second governor
floor; rates earlier in a run can differ. No lease misses were reported. The
headless steady medians below exclude presentation and use 120 animated frames
per repeat, sway every third frame, time starting at 12, fixed supersample 2 and
the unchanged 0.5-pixel settling threshold:

| Raster | Planted compose / sway | Reef compose / sway | Pool compose / sway |
| --- | ---: | ---: | ---: |
| 300×210 | 0.90 / 5.80 ms | 1.03 / 0.21 ms | 0.92 / 0.99 ms |
| 590×380 | 1.69 / 10.72 ms | 1.68 / 0.59 ms | 1.74 / 2.02 ms |
| 880×576 | 2.83 / 16.27 ms | 2.57 / 1.03 ms | 2.99 / 3.41 ms |

The planted 590×380 third repeat was slower (2.45 / 15.21 ms); it is preserved
in the raw evidence rather than silently discarded. No claim is made about the
cause of the headless/native or run-to-run timing differences.

## 1. Avoid band dispatch on quiet Rock Stack frames

Locations: `games/zenconstruction/src/site.cpp:1527`,
`shared/render/src/r3d_renderer.cpp:372`, `:563`, `:567`;
cadence `games/zenconstruction/src/zen_view.cpp:704`.

`Site` enables a renderer pool with up to seven workers plus the caller.
Every sufficiently large draw wakes the pool, partitions the whole screen into
up to 48 bands, and traverses the prepared triangle queue per band. Triangles
overlapping multiple bands repeat attribute-plane and mip setup. Quiet rendering
has a 100 ms interval; its small wall-time improvement does not justify this
extra CPU work on the measured M4.

The initial deterministic seed-7 experiment used 50 settled rocks, fixed camera,
changing scene time, 120 frames per mode and three alternating repeats:

| Raster | Serial CPU | Threaded CPU | CPU saved | Serial wall | Threaded wall | Wall increase |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 550×360 | 6.268 ms | 10.485 ms | 4.217 ms / 40.2% | 6.283 ms | 5.290 ms | 0.993 ms |
| 733×480 | 7.841 ms | 14.303 ms | 6.462 ms / 45.2% | 7.860 ms | 6.111 ms | 1.749 ms |
| 960×600 | 9.829 ms | 18.907 ms | 9.077 ms / 48.0% | 9.849 ms | 7.028 ms | 2.821 ms |

733×480 corresponds to Rock Stack's 1.5-points-per-scene-pixel policy for a
1100×720 view. These are render-stage measurements, not total app CPU savings.

After implementation, the extended probe alternated all three scheduling modes
in the **same rebuilt binary**, avoiding a before/after compiler or code-layout
confound. Final medians of threaded nonquiet mode versus automatic quiet mode:

| Raster | Threaded CPU → quiet CPU | CPU reduction | Threaded wall → quiet wall |
| --- | ---: | ---: | ---: |
| 550×360 | 8.556 → 5.677 ms | 33.6% | 4.678 → 5.684 ms |
| 733×480 | 11.550 → 7.170 ms | 37.9% | 5.393 → 7.177 ms |
| 960×600 | 15.010 → 9.132 ms | 39.2% | 6.142 → 9.147 ms |

Native paired application result, three runs of each preserved build using
fresh copies of the same save snapshot:

| Build | Busy samples | Median | Whole-run native draws |
| --- | --- | ---: | --- |
| Before | 1671 / 1724 / 1731 | 1724 | 234 / 233 / 234 |
| After | 1144 / 1100 / 965 | 1100 | 234 / 233 / 233 |

That is **36.2% fewer median busy samples**, not a process-CPU-percent reading.
The unchanged cadence code and similar draw counts rule out intentionally
dropping frames as the source of the saving. Native captures show the same
loaded site; exact equality is established at deterministic headless times,
not by comparing captures taken at slightly different animation times.

Implementation: save the renderer's requested `threads` setting, suppress it
only for `still && camera_same`, render normally, then restore it. A cache miss
(including resize, camera movement, sign update and explicit invalidation) keeps
the original path. A busy or moving physics/crane state also keeps it. No new
thread implementation, simulation gate or render cadence was introduced.

Verification: the original serial/threaded experiment compared **every float**
of color and depth at twelve changing times per size, with zero differences.
All six timing runs per size also produced the same presented-byte checksum:
`0b1d6e86a6db95b3`, `775e5ee8ca2e02ec`, `da54e8ba430b3b73`, respectively.
The extended probe compares explicit serial, explicit threaded and automatic
quiet rendering; it also covers cache misses/hits, alternating sizes, camera
angles, hover, mood, zero/nonzero animation time, and verifies unchanged saves.
All 120 final color/depth comparisons passed (40 per size), with the same three
presented checksums as the baseline. This includes 72 scheduling comparisons
and 48 cache/camera/resize/hover/mood comparisons. The renderer's requested
thread setting is checked after the transitions, and all three save checks pass.

Limit: serial filling deliberately trades a small amount of completion latency
for less work. Only the M4 was measured. Very large rasters or much slower CPUs
may warrant a measured workload threshold later; this is not evidence for
turning threading off globally or for changing active interaction.

## 2. Retain shadow results at receivers, not just shadow-map casters

Locations: `games/zenconstruction/src/site.cpp:1038`, `:1557`;
`shared/render/src/r3d_renderer.cpp:206`, `:278`, `:287`.

Existing reuse is real: sleeping rocks and stationary props populate a kept
1024×1024 shadow map; only the previous moving-caster rectangle is restored.
However, every frame restores **unshadowed** scenery color/depth and re-runs the
deferred shadow filter on all scenery pixels. Partial shadow-map restoration
does not imply partial receiver shading.

Native top-of-stack counts for `shadow_band` were 452, 204, 464 (busy totals
1,625, 1,040, 1,682). The separate existing `rockstack_shadow_tests --bench`
measured 2.3761 ms/frame at 960×600 for restore/cast/filter in its serial fixture,
with max pixel error zero. Neither figure isolates how many receivers changed.

Proposal: retain the fully shadowed stationary layer and a receiver-to-shadow-tap
dependency structure. After restoring/recasting moving casters, compare affected
shadow texels with their previous values. Re-filter only receivers whose 2×2
footprints intersect changed texels; restore their unshadowed colors before
applying the filter. Camera, size, light, static geometry and material changes
invalidate the corresponding data. Start with tiles and conservatively enlarge
coverage for the filter footprint and boundary taps. Avoid a full map comparison
if the caster's dirty bounds suffice.

**Do not simply freeze the map when `Run::quiet()` is true.** The parked operator
looks/sways (`crane_model.cpp:386`), the aerial and pennant move (`:635`), and the
wind cups rotate (`:753`). They are included in the dynamic crane caster ranges.
The quiet headless sequence changed on average 26,027.5 presented pixels per
0.1-second step at 733×480 (including the brook). A whole-scene freeze would lose
real authored animation.

Not implemented. Required validation: compare raw color/depth and presented
pixels against a complete shadow rebuild across animation, camera orbit/zoom,
caster departure, map edges, hovering rocks, resizing and site replacement.
Keep the existing shadow tests and 1024 map / 2×2 filtering unchanged. The
dependency count, additional memory and actual savings still need measurement.

## 3. Stop re-preparing stationary geometry and shading hidden receivers

Locations: `games/zenconstruction/src/site.cpp:991`, `:999`, `:1100`, `:1228`;
`shared/render/src/r3d_renderer.cpp:464`, `:567`.

The cached scenery excludes the bowl, boulder, sign, sleeping rocks and crane
body. Those are transformed, lit, culled, queued and filled again even when only
small decorative parts and the brook change. `draw` does vertex work before
back-face and screen rejection; `fill_band` recomputes planes/mips per intersected
band. The representative native run spent 352 samples in `draw`, 324 in
`fill_band<9>` (textured/shadowed) and 159 in `fill_band<8>` (untextured/shadowed),
versus 37 in the view's present loop. These costs overlap finding 1's removed
band work and include necessary moving content.

Proposal: split invariant geometry/preparation from changing lighting. Retain
projected stationary meshes and per-triangle planes until camera/material/model
changes; bin retained triangles into intersecting bands rather than searching
the entire queue. Eventually retain stationary color/depth/receiver data so the
bowl and sleeping rocks occlude scenery **before** shadow filtering. Keep an
ordered dynamic composition path for brook transparency, glass, outlines and
hovering highlights; a flat “static picture + brook” layer is insufficient.
The renderer's `shade_vertex` result is also replaced on the shadowed path
(`:489–507`), but compiler elimination must be checked before counting that as
saved work.

Not implemented beyond finding 1. Acceptance requires exact renders at multiple
cameras, near/far rock mesh transitions, waking/fetching/releasing rocks,
hover colors, crane poses and glass overlaps. Reordering floating-point blending
or affine plane evaluation is not automatically pixel-equivalent. Do not change
rock construction or the immutable `call_once` mesh/texture caches.

## 4. Further exact foliage rejection, concentrated on the planted tank

Locations: `shared/ambient/src/stage.cpp:723`, `:863`, `:925`;
`shared/ambient/src/raster3d.hpp:171`;
`shared/ambient/src/motion.cpp:139`.

At 590×380 the planted archive has 126,426 foliage triangles; 8,106 are already
settled into the fixed layer and 33,908 conservatively hidden for their complete
sway envelope. The remaining 84,412 enter the moving draw order. Existing work
already projects each used vertex once, keeps motion constants and rest lighting,
and solves integer row spans rather than visiting the whole triangle rectangle.
Repeating those optimizations is not a new saving.

Measured native planted samples: Gouraud scan 191/189/207, sway setup 65/71/65.
Headless sway at 590×380: planted 10.72 ms, reef 0.59 ms, pool 2.02 ms. A change
aimed at grass therefore needs planted-tank evidence; it cannot be justified by
reef numbers alone.

Proposal: measure current-pose conservative tile occlusion before rasterizing
the remaining foliage, and retain a visibility result for fully opaque leaves
so overdraw does not repeatedly shade them. Translucent tips must retain their
existing order and integer blend results. Restrict root-motion updates to roots
actually used by moving vertices if counters show a useful excluded population.
The inner edge sign test after exact span solving also appears redundant; treat
removing it as a separately measured experiment, not the main saving.

Not implemented. Required proof: exact frame comparisons over sway extrema,
all three tanks and detail sizes, partially transparent tips, near clipping,
resize/adopt and concurrent build/compose. Do not enlarge the settle threshold,
reduce foliage, change sampling or lower frame/sway rates to improve a number.

## 5. Actor visibility is a history of passing fragments, not final visibility

Locations: `shared/ambient/src/stage.cpp:193`, `:1072`, `:1172`;
`games/stillwater/src/riverscape_look.cpp:387`.

`FragmentPolicy::cover` appends every fragment that passes the depth test at
that point in traversal. A later nearer fragment does not remove the earlier
entry. The shading pass evaluates all entries, including ones subsequently
overwritten by opaque fish/crab surfaces. Planted measured 4,990 actor fragments
per frame at 590×380. Native reef median samples were 105 in fragment scanning,
185 in `draw_actors` and 24 in the out-of-line shader; the complete headless
compose stage cost 1.68 ms/frame. The redundant subset was not instrumented, so
these are an opportunity bound, not an expected saving.

Proposal: give the renderer an explicit, conservative opacity contract from the
Look, retain the last opaque fragment index per pixel, and shade only that
fragment and the later translucent history in original order. This preserves
the current rounded blends. Do not infer opacity from `closed_actor`: that is
a back-face-culling contract. Fish fins return alpha 0.55–0.90 and need the
underlying color. Reversing blend order or merely keeping nearest depth would
change the picture. Per-creature heading sin/cos and per-part invariant data can
also be prepared once rather than repeated per part, but their separate cost
has not been isolated.

Not implemented. Compare final pixels against the complete fragment-history
path over overlapping fish/fins/crabs, startles, all tanks, caustic phases and
near-plane crossings. No simulation or saved-creature state should change.

## 6. Fixed-layer build repeats per-triangle work per visible sample

Location: `shared/ambient/src/stage.cpp:430–516`, especially `:480–507`.

After visibility is known, each sample reconstructs the same triangle's world
corners, decodes its normals/albedo, builds its UV tangent frame, projects its
three corners again and computes `log2` texture LOD. Tangents, bitangents and
this LOD depend on the triangle/projection, not the sample. At supersample 2,
there are four such sample opportunities per output pixel.

| Raster | Planted fixed build | Reef fixed build | Pool fixed build |
| --- | ---: | ---: | ---: |
| 300×210 | 27.3 ms | 37.0 ms | 29.2 ms |
| 590×380 | 64.1 ms | 89.1 ms | 81.3 ms |
| 880×576 | 126.8 ms | 172.7 ms | 170.3 ms |

Proposal: lazily prepare the constants only for visible triangles, or first try
a tiny last-triangle cache for adjacent samples. Preserve exact expression and
operation order. A large cache prepared for every triangle can lose to the
current path on heavily hidden geometry; measure memory and hit rates as well
as build time. This benefits first load and resizing, **not steady frame cost**.

Not implemented. Compare fixed color, depth, boosts, light positions and final
frames byte-for-byte at all sizes and all three archives. Existing baseline
PNGs cover time 12, three sizes, three repeats; animated-time sweeps would still
be required for a production change.

## 7. Presentation is a smaller, separate opportunity

Locations: `games/zenconstruction/src/zen_view.cpp:1533`, `:733`;
`shared/ambient/src/present.cpp:63`;
`shared/ambient/ui/scene_view.cpp:354`.

Rock Stack quantizes each source row once but repeats its column-map expansion
for every enlarged output row and allocates the temporary row every frame.
Ambient already copies duplicate enlarged rows with `memcpy`; that exact
technique can be reused. Native `present_scene` samples were 37/41/37. Planted
ambient presentation was 28/31/32 samples, with traced enlargement/publication
0.62/0.68/0.66 ms per frame. The shared `r3d_bench` measured 0.369 ms present out
of 1.066 ms total at 550×360, but its synthetic scene and 15-bit quantization
are not Rock Stack's `ZenView::present_scene` quantizer.

All production publishers here still publish a whole frame. Once renderer
damage is established, pass it through to presentation, preserving all pixels
outside the damage on every rotating buffer. Expand damage for overlays and
changed shadows; account for skipped generations and exposure.

The current pinned toolkit already provides `LiveSurfaceWriteLease::publish(Rect)`
with the unchanged-outside-damage promise, and preservation options. Do not
repeat the older `docs/PERFORMANCE.md` statement about the earlier pin as if it
described the current toolkit. Inspect and verify the current native endpoint.
Generic damage accumulation, buffer-history or scheduling gaps belong with the
GUI.Forms owner. No toolkit source was modified.

Not implemented. Row copying is low-risk but a small fraction of native cost;
the damage work depends on findings 2–5. Verify exact native-format bytes,
odd/fractional enlargement, DPI changes, 600×420, overlays and retained exposure
before claiming a native presentation saving.

## Correctness: clipped triangles with one surviving vertex

Location: `shared/ambient/src/raster3d.hpp:307`.

`rasterize` clips a triangle and then uses `scan<Policy, true>` whenever the
result has three vertices. Three vertices can mean either an untouched triangle
or one original vertex plus two intersections. `Identity=true` is valid only
for the untouched triangle: it discards the clip vertices' mapping back to the
original barycentric weights.

`tools/engine_review/clip_probe.cpp` supplies one vertex at depth 0.2, two at
0.05 and a near plane at 0.1. Its attribute is 0/1/0 at the original vertices.
It compares the public rasterizer with the general scan path using explicitly
constructed intersection weights. M4 result:

```text
covered 63, coverage differences 0, attribute differences 63, max error 0.322591782
```

Proposal: use the identity fast path only when all three original vertices are
inside; send a clipped three-vertex polygon through the general mapping. The
existing ambient test checks coverage, not these attributes. No current test
expectation needs weakening. The diagnostic is an observation tool, not a
replacement test or an assertion that the discrepancy is acceptable.

Not implemented in the scheduling change. This deserves its own attribute
regression and intentional before/after difference record, including two-inside
and all-inside controls, perspective and non-perspective policies, both windings,
and the affected fixed/foliage/actor paths. I did not establish a normal tank
camera/time that visibly exercises it or measure its production frequency.
It is a correctness issue; no speed saving is claimed.

## Boundaries, rejected shortcuts and work not completed

- `shared/render/src/r3d.hpp` and the feature-rich `r3d_renderer.cpp` have
  separate scan implementations. Rock Stack calls `Renderer::fill_band`, not
  the core header's fixed-point shader scan. Improving the latter alone would
  not explain a Rock Stack speedup. Neither was replaced during this review.
- Koi-Koi and Nature Cube use ambient archive/inflate utilities; they do not
  render their scenes through Stillwater's `Stage`. Mowing uses `SceneView` /
  cadence / presentation with its own scenery. Shared metadata is not evidence
  that all four games share the fish renderer's bottleneck.
- Keep the existing fixed/sway/frame separation, conservative foliage hiding,
  cached caustic taps, fixed caustic animation table, early depth tests,
  immutable startup caches and lazy game creation. No benefit was established
  for replacing float with double, reducing maps/meshes or tuning away motion.
- Caustic/Look construction was about 22–23 ms per tank in the headless runs.
  Table construction and small temporary allocations occur at load, not every
  frame. Serializing/prebaking or retaining bundles across tank switches might
  avoid that cost, but is not the first steady-animation optimization. Memory
  retention and invalidation need measurement. Not implemented.
- The local renderer's `std::thread` pool and Diorama's per-update `std::async`
  remain older implementations. The pinned GUI.Forms public
  `gui_forms/threading.hpp` already provides `AtomicThreadPool`, `Worker` and
  `CancellationFlag`. A toolkit-aligned migration should preserve synchronous
  band completion and asynchronous sway's double-buffer lifetime. No separate
  thread-creation cost was isolated, so no migration saving is claimed. The
  quiet-frame change adds no threads and leaves concurrent rock creation intact.
- Full stationary-object/receiver retention, final actor visibility, foliage
  tile rejection, fixed-layer preparation and native damage were not implemented.
  Their required image comparisons are specified above, not claimed as passed.
- Active crane play, arbitrary saved stacks and extreme cameras were not given
  full native CPU profiles. No Windows/Linux performance, packaged-release,
  leak, thermal/power or audio listening result is claimed. Native audio stayed
  enabled during sampling; existing audio tests are part of the full suite.

## Reproduction and verification

Review tools are outside the product build. `native_probe.py` handles isolated
saves, all three tanks, repeated samples, captures and JSON summaries;
`headless.py` records the nine tank/size combinations. `site_probe.cpp` links
the existing application build's static libraries and checks exact color/depth,
presented checksums and save preservation. Its modes are 0 serial / 1 threaded
with the nonquiet hint / 2 automatic quiet scheduling. `clip_probe.cpp` is the
standalone clipping diagnostic.

```sh
# From this authoritative worktree; use the supplied helper and only two jobs.
m4build -- sh -c 'mkdir -p .build && sh .dog-build.sh dogfood all rockstack_site_preview'

# Run on the M4 through m4build, with the same compiler/runtime as the app.
export PATH=/opt/homebrew/opt/llvm@22/bin:/opt/homebrew/bin:$PATH
export GUI_FORMS_LLVM_RUNTIME=$HOME/Developer/PlaysuiteDependencies/toolkit-dogfood/toolchain/llvm-22.1.8-macos14
cmake -S tools/engine_review -B .build/engine-review -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=$HOME/Developer/PlaysuiteDependencies/toolkit-dogfood/gui_forms/cmake/llvm22.cmake
cmake --build .build/engine-review -j2
.build/engine-review/site_probe 733 480 120
.build/engine-review/clip_probe
python3 tools/engine_review/headless.py .build/app-dogfood/sw_preview astra/engine-review/tanks
python3 tools/engine_review/native_probe.py .build/app-dogfood/games.app stillwater --scene planted
python3 tools/engine_review/native_probe.py .build/app-dogfood/games.app zenconstruction
ctest --test-dir .build/app-dogfood --output-on-failure --timeout 180 -j2
```

Baseline full suite: **73/73 passed**, including all 30 `design` tests, 96.61 s
elapsed (`astra/engine-review/ctest-before.log`). Final rebuilt full suite:
**73/73 passed**, including the same 30 `design` tests, 92.94 s elapsed
(`astra/engine-review/ctest-after.log`). Existing tests and expectations are
unchanged. This includes the cold concurrent rock-cache test, rules, physics,
shadow regression, ambient engine, Stillwater view contract and complete
collection tests.

The final 600×420 native probes passed for both games, and their captures were
visually inspected. Rock Stack recorded 281 native draws over the full fresh-site
run; Stillwater recorded 337, no missed leases, a 300×210 scene and 3.64 ms mean
draw / 0.23 ms mean present. These are minimum-size validation receipts, not a
paired performance comparison.

`python3 scripts/check-style.py` reports 164 files and zero findings; explicit
review-tool C++ checking reports two files and zero findings. Python probes also
pass `py_compile`. `git diff --check` is clean. The new diagnostic targets are
not inserted into the product or used to replace any existing test.
