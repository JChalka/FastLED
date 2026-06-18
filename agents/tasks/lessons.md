# Lessons Learned

<!-- Add lessons from corrections and discoveries here -->
- RGBW endpoint planning: do not conflate 3-channel/interior physical-headroom
  scaling with locked dual-edge unreachable-target policy. The former uses real
  available emitter headroom; the latter chooses clipping, rolloff, or scaled
  endpoint mapping after the dual-edge target exceeds physical headroom.
- Endpoint defaults: 3-channel/interior strict sub-gamut outputs should default
  to `headroom_fit_scale`; locked dual-edge outputs should default to
  `y_correct_clip`, with rolloff and full-endpoint scaling as explicit policies.
  RW/GW/BW-style inner boundaries are narrow cached dual-edge lines, not
  3-channel solves.
- Dual-edge `y_correct_clip` must preserve the solved chromaticity ratio by
  capping the uniform scale when any active diode reaches physical max. Do not
  clip channels independently or keep increasing the weaker/stronger partner;
  that turns full yellow-like edges into physically incorrect equal-channel
  endpoints.
- Colorimetric ordering: transfer/gamma curves and power shaping may be valid
  source-side, pre-solve preparation. The forbidden operation is mutating the
  solved physical tuple after the colorimetric solve, or silently using legacy
  temperature/correction byte scaling as an implicit substitute for source
  preparation.
- RGB-in-RGBW-package solve is distinct from RGBW white extraction: run the RGB
  triangle solve on an RGBW package's RGB emitters and route as RGBW with W held
  at zero. Future interleaved RGB/RGBW output is exploratory, not part of the
  initial RGB implementation scope.
