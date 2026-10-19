// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: HDR highlight expansion (inverse tonemap) of an SDR picture, shared by
// hdr_output.frag (frames without a gain map) and hdr_gain.comp; tests/test_hdr_highlight.cpp
// compiles it as C++. Above a knee, luminance is raised smoothly (in stops) so that the game's
// brightest lights reach `headroom` times paper white; the hue stays.
// Bloodborne's lights stop near 0.7 linear (its tonemap's shoulder; its 16-bit scene holds
// nothing brighter, measured at Central Yharnam's first lamp 2026-10-19), so they, not SDR white,
// reach the peak.
// highlights (bbport.ini hdr_highlights, 0..1): 0.5 takes the lights all the way to the peak
// with the knee at 0.12 linear (sRGB ~0.38); higher values lower the knee to 0.06 (more of the
// picture brightens: lamp halos, lit walls).

const float LightsTop = 0.7; ///< linear luminance of the game's brightest lights

// The SDR picture's encoded value as light: the gamma 2.2 display the game was graded on (the
// sRGB curve's straight toe lifts the dark shades Bloodborne is made of; Kaniel 2026-10-19).
float SdrToLinear(float c) {
    return pow(max(c, 0.0), 2.2);
}

float HighlightGain(float luma, float headroom, float highlights) {
    if (highlights <= 0.0 || headroom <= 1.0) {
        return 1.0;
    }
    const float h = clamp(highlights, 0.0, 1.0);
    const float knee = mix(log2(0.25), log2(0.06), h); // in stops
    const float t = clamp((log2(max(luma, 1e-6)) - knee) / (log2(LightsTop) - knee), 0.0, 1.0);
    // The lights' luminance at t = 1: toward the peak, all of it from 0.5 on.
    const float top = mix(LightsTop, headroom, min(1.0, 2.0 * h)) / LightsTop;
    // Smooth start (t^2); never past the peak.
    return min(mix(1.0, top, t * t), max(headroom / max(luma, 1e-4), 1.0));
}
