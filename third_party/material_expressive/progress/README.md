# Expressive wavy progress reference

Pinned AndroidX source snapshots document the default Material 3 Expressive
progress dimensions, colors and amplitude thresholds. The originals retain
their Apache-2.0 notices; source URLs and SHA-256 hashes are in provenance.json.

Native interfaces: `ui/md3_progress.h`. Both forms use primary for the active
indicator and secondaryContainer for the remaining track. Linear defaults are
240 × 10dp, 4dp active/track/stop width, 4dp gap, 3dp peak amplitude, 40dp
determinate / 20dp indeterminate wavelength. Circular defaults are 48dp,
4dp active/track width and gap, 1.6dp peak amplitude, 15dp wavelength.

The C++ renderer constructs bounded sampled sine waves with rounded caps and
separated track geometry. Circular waves use an integer number of lobes to avoid
a seam. It is a native implementation using the pinned tokens, not a translation
of Compose path modifiers or an assertion of pixel-identical animation. Native
indeterminate movement is deterministic from ImGui time; its exact temporal
profile differs from Compose. Caller-owned determinate progress is not smoothed.
Reduced motion, duration scale zero or animate=false selects a still phase.

Negative or non-finite progress represents unknown progress; finite values above
one clamp to complete. Wavelength, thickness and amplitude can be customized.
