// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: HDR highlight expansion (inverse tonemap) of an SDR picture, shared by
// hdr_output.frag (frames without a gain map) and hdr_gain.comp; tests/test_hdr_highlight.cpp
// compiles it as C++. Above a knee, luminance is raised smoothly (in stops) so that the game's
// brightest lights reach `headroom` times paper white; the hue stays.
// Bloodborne's lights stop near 0.7 linear (its tonemap's shoulder; its 16-bit scene holds
// nothing brighter, measured at Central Yharnam's first lamp 2026-10-19), so they, not SDR white,
// reach the peak.
// highlights (bbport.ini hdr_highlights, 0..1): 0.5 takes the lights all the way to the peak
// with the knee at 0.12 linear (sRGB ~0.38); higher values lower the knee to 0.03 and soften
// the ramp, so a dusk sky, lit windows and walls rise too (Kaniel 2026-10-19: "maybe with
// outside lighting"). 0.5 keeps them as they were.

const float LightsTop = 0.7; ///< linear luminance of the game's brightest lights

// The SDR picture's encoded value as light: the sRGB curve, as Windows shows SDR in HDR mode, so
// all but the lights look as on the desktop (Kaniel 2026-10-19: gamma 2.2 was too dark).
float SdrToLinear(float c) {
    return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

// The luminance the gain follows: a pixel's own, or less where its small area (`area`: the mean
// around it) is darker, so a thin bright edge beside dark pixels is lifted far less than a lamp's
// core (Kaniel 2026-10-19: "some small harsh edges"). Never above the pixel's own.
float LocalLuma(float luma, float area) {
    return min(luma, mix(area, luma, 0.5));
}

float HighlightGain(float luma, float headroom, float highlights) {
    if (highlights <= 0.0 || headroom <= 1.0) {
        return 1.0;
    }
    const float h = clamp(highlights, 0.0, 1.0);
    // In stops: 0.25 at 0, 0.12 at 0.5, 0.03 at 1.
    const float knee = h <= 0.5 ? mix(log2(0.25), log2(0.12), 2.0 * h) : mix(log2(0.12), log2(0.03), 2.0 * h - 1.0);
    const float t = clamp((log2(max(luma, 1e-6)) - knee) / (log2(LightsTop) - knee), 0.0, 1.0);
    // The lights' luminance at t = 1: toward the peak, all of it from 0.5 on.
    const float top = mix(LightsTop, headroom, min(1.0, 2.0 * h)) / LightsTop;
    // Smooth start (t^2, softer toward t^1.25 above 0.5); never past the peak.
    const float shape = mix(2.0, 1.25, clamp(2.0 * h - 1.0, 0.0, 1.0));
    return min(mix(1.0, top, pow(t, shape)), max(headroom / max(luma, 1e-4), 1.0));
}

// How much more saturated a lifted pixel gets (its chroma around its luminance): up to a quarter
// more at the full lift (the lights at the peak), into Rec.2020's wider gamut; 1 where the gain
// is 1, so all but the lights keep the game's colour.
float ChromaScale(float gain, float headroom) {
    const float top = max(headroom / LightsTop, 1.0001);
    return 1.0 + 0.25 * clamp((gain - 1.0) / (top - 1.0), 0.0, 1.0);
}
