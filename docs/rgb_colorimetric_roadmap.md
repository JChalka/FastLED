# RGB and RGBW Colorimetric Roadmap

Source of truth for the focused RGB/RGBW colorimetric work discussed in
FastLED issue #2748. This document is intentionally narrow: it captures the
context needed to work on the colorimetric response path without requiring a
fresh tour of the whole FastLED codebase.

Last reviewed: 2026-06-18

## Issue Context

The current request comes from the reopened #2748 discussion after the RGBW
strict sub-gamut fixes. Zackees' acceptance criterion for the RGB-only follow-up
is explicit: the algorithm must compress well into a discrete form.

The proposed implementation sequence is:

1. Implement RGBW sub-gamut endpoint scaling.
2. Split portable functions from RGBW colorimetric code into
   `colorimetric_response.cpp.hpp` / `colorimetric_response.h`.
3. Implement `rgb_colorimetric.cpp.hpp` / `rgb_colorimetric.h`.
4. Find the correct implementation point for RGB colorimetric output.

The intended product behavior is opt-in per strip. Existing RGB output, existing
RGBW white extraction modes, and existing user sketches must remain compatible
unless a user explicitly selects colorimetric behavior through the per-strip
configuration surface.

## Maintainer Decisions From #3255 / #3256

Zackees captured the maintainer-approved colorimetric refactor contract in
FastLED #3255 and the first extraction PR in #3256. This roadmap should align
with those decisions while preserving the verified Phase 1 RGBW endpoint work in
this fork. Do not directly copy the PR implementation; use these as the target
contract for our next refactor passes.

- **No compile gates as the final shape.** The final design drops
  `FASTLED_RGBW_COLORIMETRIC` / RGB-specific gates and relies on tree-shaking:
  solver entrypoints are installed through setters/function pointers, default to
  `nullptr`, and `--gc-sections` drops unused solver code and float math. The
  current fork may keep the old gate only as a temporary transition while Phase 2
  is underway.
- **Per-strip profiles, not global singleton profiles.** Colorimetric profile
  ownership moves into the strip/controller configuration path. Global setters
  such as `set_rgbw_colorimetric_profile` and `FastLED.setRgbwColorimetricProfile`
  are transitional and should be removed in the config-refactor pass.
- **Public surface uses `fl::variant` + `fl::shared_ptr<const ...>`.** RGBW
  should model `mode XOR colorimetric+profile` as a variant alternative such as
  `ColorimetricMode { DiodeProfilePtr profile; bool boosted; }`. RGB should use
  a separate `ColorimetricRgb { EmitterProfilePtr profile; }`. Profile lifetime
  is owned with `fl::shared_ptr<const T>` rather than raw pointers.
- **Clean break for unreleased colorimetric API.** Because this colorimetric API
  has not shipped in a release, the final refactor can remove legacy enum
  entries, fallback stubs, old macros, and global setters instead of keeping
  aliases indefinitely.
- **TINY memory tier is a hard boundary.** Per-instance colorimetric state on
  `Rgbw`, `CLEDController`, `PixelController`, and related setters must be gated
  with `FL_PLATFORM_HAS_TINY_MEMORY` so <=1KB-SRAM platforms get zero added bytes
  per controller/config. Code constructing colorimetric modes should not compile
  on TINY-tier builds.
- **Shared response namespace is `fl::colorimetric_response`.** PR 1 moves
  topology-independent helpers into `src/fl/gfx/colorimetric_response.{h,cpp.hpp}`
  under that namespace, with temporary `using` re-exports back into
  `fl::colorimetric_detail` for RGBW compatibility until the later clean-break
  pass.
- **RGB entrypoint is post-policy / pre-emit.** RGB colorimetric integration
  should hook inside `loadAndScaleRGB`: consume the bytes after existing
  correction, temperature, brightness, and gamma/power policy have produced the
  normal output RGB, then apply the RGB colorimetric solve before chipset emit.
  No later driver/gamma/correction step should mutate the solved tuple.

## Non-Goals

- Do not replace legacy RGB correction, temperature, or scaling behavior by
  default.
- Do not make baked 3D LUTs the primary runtime model.
- Do not merge RGB-only solving with RGBW white extraction policy.
- Do not make LP / W-overdrive behavior part of RGB-only solving; RGB has no
  inner white emitter.
- Do not treat layered RGBWW/RGBCCT behavior as strict sub-gamut output. The
  current RGBWW implementation is an overdrive/layered family until a direct
  strict RGBCCT candidate solver exists.
- Do not keep compile-time colorimetric gates as the final API shape. The final
  direction is runtime/per-strip opt-in plus linker garbage collection, with
  `FL_PLATFORM_HAS_TINY_MEMORY` gating for per-instance state.

## Existing Anchors

- `src/fl/gfx/colorimetric_response.h` is the Phase 2 shared-response home for
  reusable colorimetric primitives. The target namespace is
  `fl::colorimetric_response`, with temporary re-exports into
  `fl::colorimetric_detail` allowed during migration.
- `src/fl/gfx/rgbw_colorimetric.h` owns RGBW-specific topology helpers,
  `ProfileCache`, LUT table declarations, and RGBW/RGBCCT declarations.
- `src/fl/gfx/rgbw_colorimetric.cpp.hpp` owns the heavy implementations behind
  `FASTLED_RGBW_COLORIMETRIC`: `ProfileCache`, source-space scaling,
  projection, strict sub-gamut solving, LP legacy, overdrive, LUT, and RGBCCT.
- `src/fl/gfx/rgbw.h` and `src/fl/gfx/rgbw.cpp.hpp` expose the 4-channel RGBW
  public surface and dispatch.
- `src/fl/gfx/rgbww.h` and `src/fl/gfx/rgbww.cpp.hpp` expose the 5-channel RGBWW
  public surface and dispatch.
- `src/pixel_controller.h` is the central load, scale, dither, RGBW, and RGBWW
  pixel-output hub. RGBW/RGBWW conversion happens in grouped helpers;
  plain RGB drivers often call the three scalar `loadAndScale*()` methods.
- `src/cpixel_ledcontroller.h` constructs `PixelController` instances after
  controller correction, temperature, brightness, and dither settings are known.
- `src/fl/channels/options.h` stores per-channel options. Today the white-channel
  config is a variant of `Empty`, `Rgbw`, and `Rgbww`.
- `src/fl/channels/cled_controller.h` exposes per-controller configuration such
  as `setRgbw`, `setRgbww`, `setCorrection`, and `setTemperature`.
- `src/FastLED.h` currently exposes global `CFastLED` wrappers for transitional
  RGBW colorimetric settings. The #3255 target removes global profile setters in
  favor of per-strip configuration carriers.
- `tests/fl/gfx/rgbw_colorimetric.cpp` is the current test model for public
  colorimetric primitives and dispatch wiring.

## Discrete-Form Requirement

For this work, "compresses well into a discrete form" means the final RGB-only
and RGBWW/RGBCCT follow-up algorithms should be expressible as bounded,
MCU-friendly transforms:

- A small fixed set of branches, not an unconstrained optimizer.
- Profile data precomputed once per active profile / source gamut.
- Runtime work based on fixed matrix-vector operations, 3x3 solves, projection
  to a triangle, normalization, and quantization.
- For RGBWW/RGBCCT, ambiguous strict candidates should be selected by explicit
  policy branches using precomputed profile data, not by a per-pixel global
  optimizer.
- Optional LUT support, if added, must have a clear memory formula and bounded
  grid size like the RGBW LUT path.
- Sketches that do not install colorimetric profiles must not retain solver code
  or float math after linker garbage collection.

## Clarified Design Decisions

- **Emitter profiles:** replace the RGBW-specific mental model with an arbitrary
  channel-order profile. A future generalized `DiodeProfile` shape should be
  defined by channel order, channel count, and per-channel xyY data, supporting
  grayscale/single-emitter through RGB, RGBW, RGBWW/RGBCCT, and 5+ emitter
  packages. Channel order determines channel count; channel count determines
  which topology families are available.
- **Compile gates:** the target architecture has no RGB/RGBW colorimetric
  compile gates. Runtime setters install function pointers, and unused solvers
  are dropped by linker garbage collection. Use `FL_PLATFORM_HAS_TINY_MEMORY`,
  not feature macros, to eliminate per-instance state on tiny boards.
- **RGB solve ordering:** maintainer decision is post-policy / pre-emit for RGB.
  The RGB path consumes `loadAndScaleRGB` bytes after correction, temperature,
  brightness, and gamma/power policy have produced the normal output RGB, then
  applies the colorimetric solve before chipset serialization. The important
  invariant remains: no post-solve mutation may skew the solved tuple.
- **Endpoint policy scope:** 3-channel/interior `headroom_fit_scale` should be
  the strict sub-gamut default because it uses real physical headroom that would
  otherwise be lost. Dual-edge unreachable-endpoint policy defaults to
  `y_correct_clip`; `rolloff_after_clip` and `scale_to_full_endpoint` are
  explicit per-profile and/or global policy choices.
- **3-channel headroom naming:** call the separate 3-channel/interior physical
  headroom restoration path `headroom_fit_scale` or similar. Do not describe it
  as clipped-channel policy.
- **RGBCCT ambiguous regions:** strict RGBCCT overlap handling should prefer the
  least-branching runtime policy that still behaves deterministically. Candidate
  decisions should be precomputed or cached when they are lightweight enough, so
  the per-pixel path avoids expensive policy scoring.
- **RGB integration:** RGB colorimetric mode should be driver-independent and
  live in `loadAndScaleRGB` so RGB chipsets do not need individual driver edits.
  The solved values should not be changed after colorimetric application.

## Phase 1: RGBW Endpoint Scaling and Endpoint Policy

Status: closed for Phase 1 as of 2026-06-18. The closeout verifier provides
enough topology/headroom evidence to move on to Phase 2. Remaining dE failures
are treated as measurement/cache-quality follow-up, not as blockers for the
endpoint-scaling work.

This phase has two separate endpoint concepts that must not be conflated:
3-channel physical-headroom scaling and 2-channel unreachable-edge policy
selection.

Concept split:

- **3-channel / interior `headroom_fit_scale`:** 3-channel source inputs, i.e.
  inputs that always produce W participation in strict sub-gamut mode, should
  scale the solved full endpoint to the available physical headroom implied by
  the input values. Example: `h315_s015_v100` can legitimately raise W from the
  split-energy result toward full source drive while uniformly scaling the
  participating channels. This is the default behavior because all active
  channels still have real headroom; failing to do it simply throws away device
  capability. It is not a clipping-policy decision.
- **2-channel / dual-edge unreachable endpoint policy:** the requested target-Y
  physically cannot be achieved by the locked dual edge without clipping. A
  half-scale yellow-like solve can mathematically land at a clipped/full endpoint
  such as `R≈65535, G≈32768`; from there the implementation must choose a mapping
  policy. The default is `y_correct_clip`, which tracks the requested Y until a
  participating channel reaches physical max. `rolloff_after_clip` and
  `scale_to_full_endpoint` remain explicit opt-in choices. Dual-edge policy
  applies to all solving modes as it is not overdrive or a W-related policy.

Landed work:

- `src/fl/gfx/rgbw_colorimetric.cpp.hpp` contains strict/interior
  `headroom_fit_scale`, guarded by `FASTLED_RGBW_COLORIMETRIC_HEADROOM_FIT_SCALE`.
  The legacy `FASTLED_RGBW_COLORIMETRIC_STRICT_PRESERVE_INPUT_MAX` macro remains
  a compatibility alias.
- Locked native dual edges route through `RgbwColorimetricDualEdgePolicy`,
  defaulting to `YCorrectClip` with explicit `RolloffAfterClip` and
  `ScaleToFullEndpoint` modes.
- `YCorrectClip` now uses source-aligned demand scaling
  (`max(source_i / full_i)`) instead of `min(input) / min(full)`. This fixes the
  chartreuse-style headroom loss where the solved dual-edge ratio could still be
  raised uniformly until the true limiting diode reached physical max.
- `YCorrectClip` preserves the solved channel ratio by capping the uniform scale
  once any participating channel reaches physical max. It must never increase
  one channel independently after another clips.
- Interior `RW`, `GW`, and `BW` boundary lines are checked with a narrow line
  tolerance before sub-gamut triangle routing.
- The public/global policy selector is exposed through
  `set_rgbw_colorimetric_dual_edge_policy`,
  `get_rgbw_colorimetric_dual_edge_policy`, and `FastLED` wrappers.

Closeout verifier evidence:

- Closeout run:
  `fastled_rgbw_nativegamut_d65whitepoint_subgamut_model_fullpatch_y_correct_clip_phase1closeout.csv`.
- Overall pass count improved from 760/1885 to 1309/1885 after the latest patch
  set and xyY cache update. Mean dE dropped from about 4.50 to about 1.85; max
  dE dropped from about 41.95 to about 19.69. These numbers are useful trend
  evidence only; dE is not the gate for closing Phase 1.
- Native single-channel identity is preserved. All single-channel rows emit the
  requested native channel only, with W and inactive RGB channels held at zero.
- Locked native dual-edge topology is preserved. All 257 dual-channel rows emit
  only the two active RGB channels; W and the inactive primary remain zero.
- All 37 full-scale dual-edge rows now reach an output max of 65535. The prior
  run had 12 full-scale dual-edge rows that plateaued early because of the
  `min(input) / min(full)` cap.
- The motivating dual-edge rows now use the available legal headroom:
  `chartreuse` changed from approximately `32768/37514/0/0` to
  `65535/64687/0/0`; `azure` changed from `0/32768/40175/0` to
  `0/55237/65535/0`; `violet` changed from `46691/0/32768/0` to
  `65535/0/38374/0`.
- Yellow full-scale behavior remains ratio-preserving rather than raw
  equal-channel output. `yellow` and `yellow_half` solve to approximately
  `65535/32344/0/0`, not `65535/65535/0/0`.
- Strict/interior headroom fitting is closed out separately from dual-edge
  policy. All 1611 three-channel/interior rows emit one of `RGW`, `RBW`, or
  `GBW`; all 1611 have `max(output) == max(input)`.
- No strict sub-gamut row emits all four RGBW channels.

Non-blocking follow-ups:

- `RolloffAfterClip` should expose a user-facing rolloff strength / granularity
  control. The current rolloff behavior is assumed acceptable for closeout, but
  users should be able to tune how quickly the policy blends toward the scaled
  endpoint after the Y-correct clip point.
- Add direct solver regression coverage for the now-verified motivating rows:
  chartreuse, azure, violet, yellow, yellow_half, native singles, and the
  `h315_s015_v100`-style interior headroom case. These tests should assert
  topology, endpoint/max-channel behavior, and ratio preservation rather than
  real-world dE.
- Repeat physical measurements after sensor cleaning/replug and spectrophotometer
  correction. Current dE failures such as blue/single-primary drift are noted as
  measurement/cache-confidence issues, not Phase 1 endpoint-policy failures.

Acceptance criteria status:

- Strict RGBW 3-channel/interior outputs use `headroom_fit_scale` by default
  without being governed by the dual-edge clipping policy selector: **met**.
- Locked dual-edge outputs default to `y_correct_clip`; rolloff and
  full-endpoint scaling are explicit policy selections: **met**.
- Neither headroom scaling nor dual-edge clipping/rolloff/full-endpoint mapping
  introduces inactive channels: **met**.
- Existing strict sub-gamut chromaticity/topology behavior remains intact under
  both endpoint mechanisms: **met for topology/headroom; dE remains measurement
  follow-up**.
- Direct dual-edge and 3-channel/interior tests still need to be added to the
  host test suite, but verifier evidence is sufficient to move Phase 2 forward.

Likely files:

- `src/fl/gfx/rgbw_colorimetric.cpp.hpp`
- `src/fl/gfx/rgbw_colorimetric.h`
- `src/fl/gfx/rgbww.cpp.hpp`
- `tests/fl/gfx/rgbw_colorimetric.cpp`

## Phase 2: Extract Portable Colorimetric Response Math

Status: active. First extraction landed in this fork, but it must be aligned
with the #3255/#3256 target shape before Phase 2 is complete. Shared inline CIE /
matrix / geometry / NNLS primitives now live in `colorimetric_response.h` under
the transitional `colorimetric_detail` namespace. The target namespace is
`fl::colorimetric_response` with temporary re-exports back into
`colorimetric_detail` during migration. RGBW-specific topology, cache, LP,
overdrive, and RGBCCT declarations remain in the RGBW files.

Goal:

Move math that is not specifically "RGB to RGBW white extraction" into a shared
colorimetric response layer. RGBW, RGBWW, and future RGB-only code should call
that layer instead of making `rgbw_colorimetric.*` the permanent home for
general color response math.

Target file shape:

- `src/fl/gfx/colorimetric_response.h`
- `src/fl/gfx/colorimetric_response.cpp.hpp`
- Include the `.cpp.hpp` once from `src/fl/gfx/_build.cpp.hpp` if it contains
  non-inline definitions.

Landed work:

- `src/fl/gfx/colorimetric_response.h` currently owns reusable inline helpers:
  `cct_to_xy`, `xyY_to_XYZ`, `invert3x3`, `matvec3`, `barycentric_xy`,
  `quantize_u8`, `build_source_matrix`, and `nnls3`.
- `src/fl/gfx/rgbw_colorimetric.h` now includes the shared response header and
  keeps only RGBW-specific helpers/types/declarations.
- `src/fl/gfx/colorimetric_response.cpp.hpp` exists as the future single-include
  surface for non-inline shared response implementation code and is listed in
  `src/fl/gfx/_build.cpp.hpp`.

Remaining work:

- Move the shared helpers into `namespace fl::colorimetric_response` and add a
  temporary using-declaration bridge in `fl::colorimetric_detail` so existing
  RGBW callers/tests remain stable until the clean-break config refactor.
- Move `hermite_basis`, `quantize_lut_cell`, `kLutQ`, `kLutStrideBilinear`,
  `kLutStrideHermite`, and `LutInterp` into `colorimetric_response` per #3255
  PR 1. `LutTable`, `build_lut`, and `lookup_lut` remain RGBW-specific.
- Add an RGB-only `EmitterProfile` struct in `colorimetric_response.h` so Phase 3
  can add the RGB solver without another structural reshuffle.
- Add dedicated `tests/fl/gfx/colorimetric_response.cpp` coverage for the moved
  primitives, independent of RGBW topology. Existing `rgbw_colorimetric` tests
  should continue to pass through the temporary re-export bridge.
- Decide whether source-RGB-to-absolute-XYZ helpers and generalized emitter-cache
  records should move before RGB-only implementation, or whether they should land
  with the first RGB solver pass.
- Keep RGBW/RGBWW behavior unchanged while extracting further helpers.
- Avoid moving RGW/RBW/BGW routing, LP, overdrive, RGBW LUT layout, or RGBCCT
  layered behavior until a concrete shared abstraction needs them.

Candidate reusable pieces to extract:

- CIE helpers: `cct_to_xy`, `xyY_to_XYZ`. **Landed.**
- Matrix helpers: `invert3x3`, `matvec3`, `build_source_matrix`. **Landed.**
- Geometry helpers: `barycentric_xy`, RGB triangle projection helpers.
  `barycentric_xy` is landed; projection helpers remain RGBW-local for now.
- Source RGB to absolute emitter-domain XYZ conversion. **Not moved yet.**
- Profile/cache data for arbitrary channel-order emitter profiles, starting from
  physical RGB emitters and extending to grayscale, RGBW, RGBCCT, and future 5+
  emitter sets. **Not moved yet.**
- Quantization / LUT helper pieces shared by future RGB/RGBCCT LUT work:
  `quantize_u8` is landed; `quantize_lut_cell`, `hermite_basis`, `kLutQ`,
  `kLutStride*`, and `LutInterp` still need to move per #3255/#3256.

Pieces that should stay RGBW-specific unless proven otherwise:

- RGW / RBW / BGW sub-gamut routing.
- `P_W`, `d_W`, `P_RGW_inv`, `P_RBW_inv`, `P_BGW_inv`.
- `wx_lp_legacy`, boosted overdrive, and white extraction policy.
- RGBW/RGBWW LUT cell layouts unless an RGB-specific LUT is explicitly designed.

Design constraints:

- Keep headers small and dependency-light.
- Preserve current RGBW behavior while extracting. The old
  `FASTLED_RGBW_COLORIMETRIC` gate is transitional only; the final config pass
  removes it.
- Avoid adding public API before the RGB integration point is chosen.
- Keep default-build binary size stable when colorimetric support is off.

Acceptance criteria:

- RGBW and RGBWW public behavior is unchanged after the split.
- The shared layer can build an emitter basis from channel order + per-channel
  xyY data without hardcoding W as a required concept.
- Shared helpers live in `fl::colorimetric_response`, with temporary
  `colorimetric_detail` re-exports only until the clean-break refactor.
- `tests/fl/gfx/colorimetric_response.cpp` covers moved helpers independently of
  RGBW topology.
- No new heap allocation appears in the per-pixel hot path.
- The split is mechanical enough that Phase 3 can reuse the helpers without
  dragging in RGBW white extraction.

Likely files:

- `src/fl/gfx/colorimetric_response.h`
- `src/fl/gfx/colorimetric_response.cpp.hpp`
- `src/fl/gfx/rgbw_colorimetric.h`
- `src/fl/gfx/rgbw_colorimetric.cpp.hpp`
- `src/fl/gfx/rgbww.cpp.hpp`
- `src/fl/gfx/_build.cpp.hpp`
- `tests/fl/gfx/rgbw_colorimetric.cpp`

## Phase 3: Implement RGB-Only Colorimetric Solver

Status: planned, blocked on the shared response layer from Phase 2.

Goal:

Add an RGB-only colorimetric solve that uses the same source-space and measured
emitter-domain model as RGBW, but solves the single physical RGB triangle only.
There is no W channel, no LP legacy path, and no boosted W-overdrive mode.

This phase has two immediate output targets:

- **RGB device solve:** solve against the physical RGB emitter triangle and emit
  RGB for ordinary 3-channel strips / SPI devices.
- **RGB in RGBW package solve:** run the same RGB triangle solve using the RGB
  emitters in an RGBW package, then route the solved tuple through the RGBW
  output pipeline with W held at zero. This is useful immediately because the
  current test bench uses RGBW LEDs: it lets the RGB solver be verified on the
  same measured package as long as routing preserves the solved RGB tuple and
  does not invoke white extraction.

Reference algorithm:

```text
solve_rgb_only(source_rgb):
    value = max(source_rgb)
    if value is near zero: return black

    target_xyz = source_rgb_to_led_absolute_XYZ(source_rgb)
    if target chromaticity is outside RGB hull:
        project target to RGB hull

    rgb_drive = P_RGB_inv * target_xyz
    clamp tiny negatives to zero
    normalize if any channel exceeds 1
    return rgb_drive
```

Open design point:

- If out-of-hull projection or normalization can collapse value ramps, use the
  same endpoint-first value/chroma principle as the strict RGBW solver: solve the
  full-chroma endpoint, normalize/project there, then apply source value.
- If the target remains in the RGB hull and the solve is already linear, direct
  source-value solving may be sufficient. This should be decided by tests and
  verifier rows rather than assumption.

Proposed file shape:

- `src/fl/gfx/rgb_colorimetric.h`
- `src/fl/gfx/rgb_colorimetric.cpp.hpp`

Possible public/private API shape:

- Internal solver first, public dispatch later:
  `fl::colorimetric_response::solve_rgb_colorimetric(...)` or equivalent once
  Phase 2 namespace alignment is complete.
- Use the new RGB-only `EmitterProfile` / `EmitterProfilePtr` shape from #3255.
  RGB consumes a 3-emitter profile and should not require a W field or RGBW
  profile layout.
- No RGB compile gate in the final design. The RGB solver is installed via the
  runtime setter/function-pointer path and dropped by `--gc-sections` when no
  strip installs a profile.
- Per-strip public API target: `CLEDController::setRgbColorimetricProfile` taking
  `EmitterProfilePtr = fl::shared_ptr<const EmitterProfile>`, tier-gated by
  `FL_PLATFORM_HAS_TINY_MEMORY`.

Acceptance criteria:

- Pure native R/G/B remain exact identity when source primaries equal measured
  LED primaries.
- Named-gamut source colors project into the measured RGB hull without illegal
  negative drive.
- Value ramps remain monotonic and do not collapse after projection or
  normalization.
- The solver is expressible as fixed matrix operations plus bounded projection,
  satisfying the discrete-form requirement.
- The RGB-only path does not pull in RGBW LP, overdrive, or W-specific LUT code.
- RGB-in-RGBW-package mode routes the solved RGB tuple through RGBW hardware with
  W = 0, without invoking RGBW strict sub-gamut, LP, overdrive, or white
  extraction policy.

Likely tests:

- `tests/fl/gfx/rgb_colorimetric.cpp` for solver primitives and dispatch wiring.
- RGB-in-RGBW-package dispatch rows proving solved RGB reaches RGBW output as
  `R,G,B,0` after ordering, with no W participation.
- RGB hull projection rows: inside hull, outside hull, boundary points.
- Native identity rows: red, green, blue, half-red, half-green, half-blue.
- Named-gamut rows: Rec.709 / Rec.2020 / DCI-P3 source primaries against a wider
  or narrower measured LED profile.
- Value-ramp rows around projection and normalization boundaries.

Later exploration: interleaved RGB/RGBW output

Interleaved output is out of scope for the current implementation pass, but it
is worth preserving as a future research direction once RGB, strict RGBW, and
RGBWW/RGBCCT modes are stable. Strict sub-gamut RGBW always uses the W diode for
some regions, and a high-output W diode can create Y granularity gaps at low
codes: even a low-code W contribution can be tens of nits when the W emitter can
reach 1500+ nits. A pure RGB solve on the same package may land much lower, for
example around sub-nit levels, and could potentially fill low-Y granularity gaps
if a future policy can choose between RGB-only and RGBW strict outputs without
breaking chromaticity, topology, or metadata separation.

## Phase 3A: Clarify and Implement RGBCCT Strict vs Overdrive Families

Status: planned. The current RGBWW implementation should be treated as an
explicit layered/overdrive family, not as the final strict RGBCCT sub-gamut
solver.

Goal:

Separate RGBWW/RGBCCT behavior into named families so future code and verifier
data do not mix strict direct topology solves with layered warm/cool overdrive
composition.

Current implementation classification:

- `src/fl/gfx/rgbww.cpp.hpp` resolves an active `RgbcctProfile`, computes an
  eta, then calls `solve_rgbcct()`.
- `solve_rgbcct()` in `src/fl/gfx/rgbw_colorimetric.cpp.hpp` solves the target
  against the warm-W path and the cool-W path separately, then line-blends the
  resulting RGBW tuples.
- That shape is technically a layered / inner-channel overdrive family: it
  composes solved warm and cool paths rather than selecting one direct strict
  RGBCCT simplex.
- The boosted RGBWW path is also layered/overdrive because it biases eta toward
  equal warm/cool participation for more W contribution.

Strict RGBCCT sub-gamut target:

Use the RGB outer hull plus warm-W and cool-W inner emitters as a direct topology
candidate set. For a typical `RGB + WW + CW` package, strict candidates include:

- Singles: `R`, `G`, `B`, `WW`, `CW`.
- Duals: `RG`, `RB`, `GB`, `R+WW`, `G+WW`, `B+WW`, `R+CW`, `G+CW`, `B+CW`,
  `WW+CW`.
- Direct 3-channel strict candidates: `RG+WW`, `RB+WW`, `GB+WW`, `RG+CW`,
  `RB+CW`, `GB+CW`, `R+WW+CW`, `G+WW+CW`, `B+WW+CW`.

Ambiguous regions:

- Some regions, especially the `RB` side in common warm/cool layouts, may have
  overlapping valid direct solves such as `RB+WW` and `RB+CW`.
- The strict solver needs an explicit branch policy for these overlaps, not an
  unconstrained N-channel optimizer.
- The default policy should favor the least-branching MCU-friendly decision that
  can be made from cached profile data: precomputed containment order, cached
  overlap ownership, lightweight power/Y efficiency hints, headroom flags, and
  deterministic tie-breaks. More expensive policy diagnostics can live in tests
  or offline verifier tooling, not the per-pixel hot path.
- Direct inner-boundary lines such as `R+WW`, `G+WW`, `B+WW`, `R+CW`, `G+CW`,
  and `B+CW` are still 2-channel topologies. They should be cached as narrow
  boundary lines before triangle routing so they do not accidentally land in an
  adjacent 3-channel candidate.

Virtual inner-primary candidate:

- A virtual inner-primary mode is a good implementation candidate for resolving
  ambiguous warm/cool regions because it can cache virtual inner anchors at
  profile/cache build time.
- Runtime complexity can then match the RGBW strict solve shape: choose the
  containing virtual triangle and solve a fixed 3-emitter system.
- This mode is still technically inner-channel overdrive because it allows
  generated/cached virtual anchors and can permit 4-channel physical output
  after expansion.
- Therefore it must not be the default strict RGBCCT policy. It should be named
  as a separate constrained virtual-inner overdrive family.

Proposed naming direction:

- `strict_rgbcct_subgamut`: direct legal RGBCCT topology solve, one candidate at
  a time.
- `rgbcct_layered_overdrive`: current warm-path/cool-path solve plus eta blend.
- `rgbcct_virtual_inner_overdrive`: cached virtual inner-anchor solve; lower
  runtime cost than fully dynamic overdrive, but still not strict sub-gamut.

Acceptance criteria:

- Current RGBWW behavior is documented and surfaced as layered/overdrive rather
  than strict RGBCCT.
- A strict RGBCCT mode exists that selects one legal direct candidate per solve.
- Ambiguous strict regions use deterministic branch policy with no per-pixel
  unconstrained optimizer.
- Ambiguous-region decisions are cached or precomputed when practical, keeping
  runtime branch count comparable to the RGBW strict solve path.
- Virtual inner-primary mode, if implemented, is opt-in and not the default.
- Verifier/correction metadata records `model_family`, `active_channel_family`,
  and whether virtual anchors were used.

Likely files:

- `src/fl/gfx/rgbww.h`
- `src/fl/gfx/rgbww.cpp.hpp`
- `src/fl/gfx/rgbw_colorimetric.h`
- `src/fl/gfx/rgbw_colorimetric.cpp.hpp`
- Future shared response files from Phase 2.
- `tests/fl/gfx/rgbww.cpp`
- `tests/fl/gfx/rgbw_colorimetric.cpp`

## Phase 4: Find and Wire RGB Implementation Points

Status: decision clarified by #3255. RGB integrates at the post-policy /
pre-emit boundary inside `loadAndScaleRGB`; remaining work is implementation and
coverage, not choosing the conceptual insertion point.

Known RGB pipeline facts:

- `CLEDController::show` and `showColor` call `getAdjustmentData`, then construct
  a `PixelController` in `src/cpixel_ledcontroller.h`.
- `PixelController` stores raw CRGB data, dither state, and `ColorAdjustment`.
- RGBW/RGBWW paths are grouped per pixel in `loadAndScaleRGBW` and
  `loadAndScaleRGBWW`.
- Plain RGB drivers commonly call `loadAndScale0`, `loadAndScale1`, and
  `loadAndScale2` separately. Some newer/type-erased paths call grouped
  `loadAndScaleRGB` through `PixelIterator`.
- Because a colorimetric RGB solve needs all three source channels at once,
  inserting it only into one scalar `loadAndScale*()` method is unsafe.
- Maintainer decision: RGB colorimetric consumes the grouped output of
  `loadAndScaleRGB`, i.e. after existing correction, temperature, brightness,
  and gamma/power policy have produced FastLED's normal output RGB. The solve
  then runs before driver-specific byte ordering / chipset emit. Once solved,
  the driver serializes the tuple without further per-channel mutation.

Chosen integration path:

- Add tier-gated `fl::optional<ColorimetricRgb> mRgbColorimetric` to the
  per-controller path.
- Carry the optional profile into `PixelController` / `loadAndScaleRGB`.
- In `loadAndScaleRGB`, load the normal premixed RGB bytes first, then call the
  installed RGB solver function pointer when a profile is present and the
  function pointer is non-null.
- No individual RGB chipset driver edits should be necessary if they use grouped
  `loadAndScaleRGB`. Any remaining scalar-only direct driver paths need to be
  audited or migrated to the grouped loader.

Phase 4 decision criteria:

- The chosen path must preserve existing output timing for non-opt-in sketches.
- The opt-in path must not require every chipset driver to be edited at once.
- The transform must run after `loadAndScale{0,1,2}` / premixed policy output and
  before driver serialization.
- It must be clear whether RGB colorimetric applies per controller, per channel,
  globally, or some combination of those.
- The final API is per-strip/per-controller, not global. It must coexist with
  current `setCorrection`, `setTemperature`, `setRgbw`, and `setRgbww` behavior.
- The integration should be driver-independent: solve the `CRGB` values once,
  then hand solved RGB bytes/fractions to the normal driver-specific output path.

Likely files to inspect or modify during Phase 4:

- `src/fl/channels/options.h`
- `src/fl/channels/cled_controller.h`
- `src/FastLED.h`
- `src/FastLED.cpp.hpp`
- `src/cpixel_ledcontroller.h`
- `src/pixel_controller.h`
- `src/fl/chipsets/encoders/pixel_iterator.h`
- `src/fl/chipsets/encoders/pixel_iterator_adapters.h`
- Representative direct drivers that call scalar `loadAndScale*()`.

Acceptance criteria:

- Existing plain RGB output is byte-for-byte unchanged when the feature is off.
- Opt-in RGB colorimetric output is reachable through the chosen public API.
- RGB colorimetric solver output matches a synthetic chipset test byte-for-byte:
  `loadAndScaleRGB` with brightness/correction/temperature/gamma policy feeds
  the solver, and no post-solve per-channel scaling occurs.
- At least one host/stub capture path proves the transform reaches encoded RGB
  bytes, not just standalone solver output.
- The remaining direct-driver coverage gap, if any, is documented before merge.

## Verification Plan for Future Code Passes

Do not run these automatically during planning-only passes. When implementation
starts, use the project wrapper scripts rather than direct toolchain commands.

- Focused host tests for the changed area, for example `bash test rgbw_colorimetric`
  or a future `bash test rgb_colorimetric`.
- Full C++ host suite when the implementation point touches shared pixel output:
  `bash test --cpp`.
- WASM compile check for user-facing API and public headers:
  `bash compile wasm --examples Blink`.
- Platform compile checks only after the integration point is chosen and only for
  affected representative platforms.
- Hardware or verifier evidence for color quality claims. Unit tests can prove
  topology, monotonicity, and dispatch; they cannot prove real-world capture
  accuracy.

## Remaining Open Questions

- What is the preferred public API for `RolloffAfterClip` strength: a global
  setter/getter mirroring the dual-edge policy selector, a per-profile field,
  or both? This is a tuning/control follow-up, not a Phase 2 blocker.
- Should strict RGBCCT and layered RGBWW overdrive share the existing
  `RGBWW_MODE` enum, or should new names/API fields make strict vs overdrive
  impossible to confuse?
- What is the exact lightweight cached policy representation for ambiguous
  RGBCCT regions: precomputed triangle priority, compact region table, cached
  branch order, or profile-provided priority?
