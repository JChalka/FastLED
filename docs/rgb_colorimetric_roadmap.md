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
- Do not treat layered RGBWW/RGBCCT behavior as strict sub-gamut output. The
  current RGBWW implementation is an overdrive/layered family until a direct
  strict RGBCCT candidate solver exists.
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
- The compile-time-off path must not retain the heavy math in ordinary sketches.

## Clarified Design Decisions

- **Emitter profiles:** replace the RGBW-specific mental model with an arbitrary
  channel-order profile. A future generalized `DiodeProfile` shape should be
  defined by channel order, channel count, and per-channel xyY data, supporting
  grayscale/single-emitter through RGB, RGBW, RGBWW/RGBCCT, and 5+ emitter
  packages. Channel order determines channel count; channel count determines
  which topology families are available.
- **Compile gates:** keep RGB and RGBW/RGBWW colorimetric gates independent so
  users can pay only for the paths they use. Add an umbrella gate such as
  `FASTLED_COLORIMETRIC` so mixed RGB + RGBW/RGBWW sketches can enable all
  colorimetric families explicitly.
- **Pre-solve transfer/power shaping vs post-solve mutation:** colorimetric
  solvers should consume source `CRGB` values after any explicitly selected
  source-side transfer/gamma or power-scaling preparation. This mirrors display
  pipelines that de-gamma, transform color space, re-encode through 1D LUTs, and
  then apply a 3D LUT. In FastLED terms, the colorimetric solve is the 3D-LUT-like
  physical mapping stage. Legacy color temperature/correction byte scaling should
  not be silently applied as a pre-solve substitute for source preparation, and
  no gamma, correction, temperature, or power scaling should mutate the solved
  physical tuple after the colorimetric solve.
- **Endpoint policy scope:** dual-edge unreachable-endpoint policy should be
  selectable per profile and/or globally. Users should be able to choose whether
  a profile clips, rolls off, or scales a locked dual-edge endpoint.
- **3-channel headroom naming:** call the separate 3-channel/interior physical
  headroom restoration path `headroom_fit_scale` or similar. Do not describe it
  as clipped-channel policy.
- **RGBCCT ambiguous regions:** strict RGBCCT overlap handling should prefer the
  least-branching runtime policy that still behaves deterministically. Candidate
  decisions should be precomputed or cached when they are lightweight enough, so
  the per-pixel path avoids expensive policy scoring.
- **RGB integration:** RGB colorimetric mode should be driver-independent. It
  should act on the `CRGB` values before driver-specific encoding, then feed the
  solved RGB values into whatever output path the chosen LED driver already
  needs. The solved values should not be changed after colorimetric application.

## Phase 1: RGBW Endpoint Scaling and Endpoint Policy

Status: active. This phase has two separate endpoint concepts that must not be
conflated: 3-channel physical-headroom scaling and 2-channel unreachable-edge
policy selection.

Concept split:

- **3-channel / interior `headroom_fit_scale`:** the solved RGBW tuple still has
  real physical headroom. Example: `h315_s015_v100` can legitimately raise W
  from the split-energy result toward full source drive while uniformly scaling
  the participating channels. This restores use of available device headroom;
  it is not a clipping-policy decision.
- **2-channel / dual-edge unreachable endpoint policy:** the requested target-Y
  physically cannot be achieved by the locked dual edge without clipping. A
  half-scale yellow-like solve can mathematically land at a clipped/full endpoint
  such as `R≈65535, G≈32768`; from there the implementation must choose a mapping
  policy: clip, roll off, or scale the full endpoint while preserving
  chromaticity.

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
  the source max rather than the extracted split energy. This is the
  3-channel/interior physical-headroom case.
- Keep 3-channel/interior `headroom_fit_scale` as its own strict-subgamut
  endpoint restoration path. Do not route it through the dual-edge
  clipping/rolloff/full endpoint policy selector.
- Add an explicit dual-edge endpoint luminance policy for locked 2-channel solves
  that can select:
  - `y_correct_clip`: preserve the requested target-Y solve until physical
    clipping, then report/accept the residual.
  - `rolloff_after_clip`: follow the clipped/Y-correct result near the limit,
    then apply a smooth knee so post-clip values retain gradation instead of a
    hard plateau.
  - `scale_to_full_endpoint`: compatibility/current behavior; solve the
    chromaticity-preserving endpoint, then scale it by the source/value axis for
    smoother channel resolution.
- Confirm topology is preserved after both endpoint mechanisms for native
  singles, native dual edges, and strict interior routes.
- Apply the dual-edge endpoint-policy contract to direct locked 2-channel solves
  in RGBW, RGBCCT, and future direct 5+ emitter topologies. For future packages
  with yellow, violet, or other outer-hull emitters, the policy must distinguish
  a true anchor solve such as `RGY` or `RBV` from a physically unreachable
  two-channel fallback.
- Record endpoint policy metadata in any verifier rows, LUT summaries, and
  pass/fail dictionary keys that compare model output against measurements.
- Add or update focused regression coverage once code changes are allowed.

Acceptance criteria:

- Strict RGBW 3-channel/interior outputs use `headroom_fit_scale` or equivalent
  available-physical-headroom restoration without being governed by the
  dual-edge clipping policy selector.
- Locked dual-edge outputs follow the selected endpoint policy, and the
  compatibility policy preserves the full-endpoint scaling behavior.
- Neither headroom scaling nor dual-edge clipping/rolloff/full-endpoint mapping
  introduces inactive channels.
- Existing strict sub-gamut chromaticity/topology behavior remains intact under
  both endpoint mechanisms.
- Direct dual-edge tests cover clipping, rolloff, and scaled endpoint behavior.
- 3-channel/interior tests cover the `h315_s015_v100`-style physical-headroom
  restoration case separately from dual-edge policy tests.
- Verifier output is recorded in the PR or issue thread.

Likely files:

- `src/fl/gfx/rgbw_colorimetric.cpp.hpp`
- `src/fl/gfx/rgbw_colorimetric.h`
- `src/fl/gfx/rgbww.cpp.hpp`
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
- Profile/cache data for arbitrary channel-order emitter profiles, starting from
  physical RGB emitters and extending to grayscale, RGBW, RGBCCT, and future 5+
  emitter sets.
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
- The shared layer can build an emitter basis from channel order + per-channel
  xyY data without hardcoding W as a required concept.
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
- Use the generalized channel-order `DiodeProfile` direction from Phase 2. RGB
  consumes the first/declared RGB emitter basis; it should not require a W field
  or an RGBW-specific profile layout.
- Add a separate RGB compile gate, likely `FASTLED_RGB_COLORIMETRIC`, so RGB-only
  code can remain independent from the existing RGBW/RGBWW gate. An umbrella
  `FASTLED_COLORIMETRIC` gate can enable all colorimetric families for mixed
  RGB + RGBW/RGBWW sketches.

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
- The colorimetric solve should happen after any explicit source-side
  transfer/gamma or power-shaping stage, but before driver-specific byte
  ordering or chipset encoding. Once the physical RGB tuple is solved, the
  driver should serialize that tuple without applying legacy color temperature,
  correction, gamma, or power transforms that would skew it.

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
- The transform must run after any explicit source-side transfer/gamma or
  power-shaping stage and before driver serialization. Legacy correction,
  temperature, brightness, and dither semantics must be clearly defined for
  colorimetric mode rather than inherited accidentally from byte scaling.
- Current direction: feed source-prepared `CRGB` into the colorimetric path. Do
  not apply legacy color temperature/correction as hidden pre-solve scaling, and
  do not apply gamma/correction/temperature/power transforms after the solve.
- It must be clear whether RGB colorimetric applies per controller, per channel,
  globally, or some combination of those.
- The final API must remain compatible with current `setCorrection`,
  `setTemperature`, `setRgbw`, and `setRgbww` behavior.
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
- Colorimetric RGB output bypasses legacy color temperature/correction/gamma or
  power mutation after the solve; any gamma/transfer or power shaping is explicit
  and pre-solve.
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

- Should runtime opt-in be per channel, global, or both for each colorimetric
  family? Current direction supports per-profile and/or global endpoint policy,
  but controller/channel enablement still needs an API shape.
- Should strict RGBCCT and layered RGBWW overdrive share the existing
  `RGBWW_MODE` enum, or should new names/API fields make strict vs overdrive
  impossible to confuse?
- What is the exact lightweight cached policy representation for ambiguous
  RGBCCT regions: precomputed triangle priority, compact region table, cached
  branch order, or profile-provided priority?
- Which driver-independent insertion point can transform raw `CRGB` once while
  still reaching every relevant driver path without post-solve mutation?
