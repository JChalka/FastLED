# RGB and RGBW Colorimetric Roadmap

Source of truth for the focused RGB/RGBW colorimetric work discussed in
FastLED issue #2748. This document is intentionally narrow: it captures the
context needed to work on the colorimetric response path without requiring a
fresh tour of the whole FastLED codebase.

Last reviewed: 2026-06-16

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

The intended product behavior is opt-in. Existing RGB output, existing RGBW
white extraction modes, and existing user sketches must remain compatible unless
a user explicitly selects colorimetric behavior.

## Non-Goals

- Do not replace legacy RGB correction, temperature, or scaling behavior by
  default.
- Do not make baked 3D LUTs the primary runtime model.
- Do not merge RGB-only solving with RGBW white extraction policy.
- Do not make LP / W-overdrive behavior part of RGB-only solving; RGB has no
  inner white emitter.
- Do not remove the compile-time size gate. Runtime opt-in can be explored, but
  it should complement, not replace, a compile-time gate.

## Existing Anchors

- `src/fl/gfx/rgbw_colorimetric.h` owns the reusable inline math today:
  `cct_to_xy`, `xyY_to_XYZ`, `invert3x3`, `matvec3`, `barycentric_xy`,
  `build_source_matrix`, `is_native_input_gamut`, topology helpers, LUT helpers,
  and RGBW/RGBCCT declarations.
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
- `src/FastLED.h` exposes global `CFastLED` wrappers. New global settings should
  prefer this pattern over new bare `fl::set_*` functions.
- `tests/fl/gfx/rgbw_colorimetric.cpp` is the current test model for public
  colorimetric primitives and dispatch wiring.

## Discrete-Form Requirement

For this work, "compresses well into a discrete form" means the final RGB-only
algorithm should be expressible as a bounded, MCU-friendly transform:

- A small fixed set of branches, not an unconstrained optimizer.
- Profile data precomputed once per active profile / source gamut.
- Runtime work based on fixed matrix-vector operations, 3x3 solves, projection
  to a triangle, normalization, and quantization.
- Optional LUT support, if added, must have a clear memory formula and bounded
  grid size like the RGBW LUT path.
- The compile-time-off path must not retain the heavy math in ordinary sketches.

## Phase 1: RGBW Sub-Gamut Endpoint Scaling

Status: implemented in the current local/workspace RGBW solver shape, pending
verification evidence before treating it as complete.

Landed or observed work:

- `src/fl/gfx/rgbw_colorimetric.cpp.hpp` contains a strict-mode endpoint policy
  guarded by `FASTLED_RGBW_COLORIMETRIC_STRICT_PRESERVE_INPUT_MAX`.
- The policy scales the final strict RGBW tuple uniformly when
  `max(output) < max(input)`, preserving chromaticity and topology while letting
  the largest output channel track source drive headroom.
- The policy is continuous across near-full-scale inputs rather than a special
  case for exactly `65535`.

Remaining work:

- Verify the local-fork/workspace implementation against the known motivating
  rows, especially the `h315_s015_v100` style case where W should scale toward
  the source max rather than the extracted split energy.
- Confirm opt-out behavior when
  `FASTLED_RGBW_COLORIMETRIC_STRICT_PRESERVE_INPUT_MAX=0`.
- Confirm topology is preserved after endpoint scaling for native singles,
  native dual edges, and strict interior routes.
- Add or update focused regression coverage once code changes are allowed.

Acceptance criteria:

- Strict RGBW output max tracks input max unless the opt-out macro is disabled.
- Uniform scaling never introduces inactive channels.
- Existing strict sub-gamut chromaticity/topology behavior remains intact.
- Verifier output is recorded in the PR or issue thread.

Likely files:

- `src/fl/gfx/rgbw_colorimetric.cpp.hpp`
- `src/fl/gfx/rgbw_colorimetric.h`
- `tests/fl/gfx/rgbw_colorimetric.cpp`

## Phase 2: Extract Portable Colorimetric Response Math

Status: planned, not started in this repo pass.

Goal:

Move math that is not specifically "RGB to RGBW white extraction" into a shared
colorimetric response layer. RGBW, RGBWW, and future RGB-only code should call
that layer instead of making `rgbw_colorimetric.*` the permanent home for
general color response math.

Proposed file shape:

- `src/fl/gfx/colorimetric_response.h`
- `src/fl/gfx/colorimetric_response.cpp.hpp`
- Include the `.cpp.hpp` once from `src/fl/gfx/_build.cpp.hpp` if it contains
  non-inline definitions.

Candidate reusable pieces to extract:

- CIE helpers: `cct_to_xy`, `xyY_to_XYZ`.
- Matrix helpers: `invert3x3`, `matvec3`, `build_source_matrix`.
- Geometry helpers: `barycentric_xy`, RGB triangle projection helpers.
- Source RGB to absolute emitter-domain XYZ conversion.
- Profile/cache data that applies to physical RGB emitters, independent of W.
- Quantization helpers only if they are shared by RGB and RGBW public surfaces.

Pieces that should stay RGBW-specific unless proven otherwise:

- RGW / RBW / BGW sub-gamut routing.
- `P_W`, `d_W`, `P_RGW_inv`, `P_RBW_inv`, `P_BGW_inv`.
- `wx_lp_legacy`, boosted overdrive, and white extraction policy.
- RGBW/RGBWW LUT cell layouts unless an RGB-specific LUT is explicitly designed.

Design constraints:

- Keep headers small and dependency-light.
- Preserve the current `FASTLED_RGBW_COLORIMETRIC` behavior while extracting.
- Avoid adding public API before the RGB integration point is chosen.
- Keep default-build binary size stable when colorimetric support is off.

Acceptance criteria:

- RGBW and RGBWW public behavior is unchanged after the split.
- The shared layer can build an RGB emitter basis without mentioning W.
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
  `colorimetric_detail::solve_rgb_only(...)` or a renamed shared namespace once
  Phase 2 lands.
- Reuse `DiodeProfile`'s RGB/source fields initially, ignoring W, unless Phase 2
  produces a smaller shared `RgbEmitterProfile` cleanly.
- Add a separate RGB compile gate, likely `FASTLED_RGB_COLORIMETRIC`, so RGB-only
  code can remain independent from the existing RGBW gate. A compatibility alias
  to the RGBW gate can be considered, but it should be a deliberate size/API
  decision.

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

Likely tests:

- `tests/fl/gfx/rgb_colorimetric.cpp` for solver primitives and dispatch wiring.
- RGB hull projection rows: inside hull, outside hull, boundary points.
- Native identity rows: red, green, blue, half-red, half-green, half-blue.
- Named-gamut rows: Rec.709 / Rec.2020 / DCI-P3 source primaries against a wider
  or narrower measured LED profile.
- Value-ramp rows around projection and normalization boundaries.

## Phase 4: Find and Wire RGB Implementation Points

Status: research started; no implementation point has been chosen yet.

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

Candidate integration paths:

1. Per-channel opt-in setting in `ChannelOptions`.
   - Add a plain-RGB colorimetric config separate from `mWhiteCfg`.
   - Expose per-controller builder methods on `CLEDController`.
   - Expose global wrappers on `CFastLED` if a global setting is wanted.
   - Pros: matches the new Channels API direction and keeps opt-in scoped.
   - Cons: still needs a central pixel-output hook.

2. Grouped RGB loader hook.
   - Add a grouped colorimetric RGB load path beside `loadAndScaleRGB`.
   - Migrate drivers that opt in to grouped RGB loading.
   - Pros: one transform per pixel, natural place for a 3-channel solve.
   - Cons: many existing fast paths use scalar loads directly; migration must be
     careful and platform-aware.

3. PixelIterator-only first implementation.
   - Wire the transform into `PixelIterator` / scaled pixel ranges first.
   - Pros: lower template blast radius and easier host/stub capture tests.
   - Cons: does not cover every direct chipset path, so it may be incomplete as
     a final user-facing feature.

4. `PixelController` wrapper/cache strategy.
   - Create a wrapper that computes transformed RGB once per pixel and serves
     scalar byte loads from a cached transformed tuple.
   - Pros: could support drivers that still call scalar `loadAndScale*()`.
   - Cons: statefulness, lane handling, reverse traversal, dithering, and timing
     sensitivity make this the riskiest option.

Phase 4 decision criteria:

- The chosen path must preserve existing output timing for non-opt-in sketches.
- The opt-in path must not require every chipset driver to be edited at once.
- The transform must run after correction, temperature, brightness, and dither
  semantics are clearly defined. If it runs before any of those, document why.
- It must be clear whether RGB colorimetric applies per controller, per channel,
  globally, or some combination of those.
- The final API must remain compatible with current `setCorrection`,
  `setTemperature`, `setRgbw`, and `setRgbww` behavior.

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

## Open Questions

- Should RGB-only colorimetric reuse `DiodeProfile` and ignore W, or should Phase
  2 introduce a smaller shared RGB emitter/profile type?
- Should `FASTLED_RGB_COLORIMETRIC` be independent from
  `FASTLED_RGBW_COLORIMETRIC`, or should one imply the other for users who want
  all colorimetric math enabled?
- Should runtime opt-in be per channel, global, or both?
- Should the transform consume raw CRGB before existing correction/temperature,
  or corrected/scaled RGB after those settings? The RGBW path currently applies
  premixed scale through the conversion call, so matching user expectations here
  needs an explicit decision.
- Which drivers are acceptable for the first supported RGB integration path, and
  which can remain documented as not yet wired?
