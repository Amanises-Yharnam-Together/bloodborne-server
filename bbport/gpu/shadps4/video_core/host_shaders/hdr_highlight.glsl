// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: HDR highlight expansion (inverse tonemap) of an SDR picture, shared by
// hdr_output.frag (frames without a gain map) and hdr_gain.comp. Above a knee, luminance is
// raised smoothly so that SDR white reaches `white` times paper white; the hue stays.
// highlights (bbport.ini hdr_highlights, 0..1): 0.5 takes white all the way to the peak with the
// knee at 0.4 linear (sRGB ~0.66); higher values lower the knee (more of the picture brightens).

float HighlightGain(float luma, float headroom, float highlights) {
    if (highlights <= 0.0) {
        return 1.0;
    }
    const float knee = mix(0.6, 0.2, clamp(highlights, 0.0, 1.0));
    const float white = mix(1.0, max(headroom, 1.0), min(1.0, 2.0 * highlights));
    const float t = clamp((luma - knee) / (1.0 - knee), 0.0, 1.0);
    // Smooth start (t^2), white at t = 1.
    return mix(1.0, white, t * t);
}
