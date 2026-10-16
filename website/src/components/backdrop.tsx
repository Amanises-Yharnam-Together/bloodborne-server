/*
 * The page's backdrop, behind everything: a pale moon, a far city of spires in the mist, near
 * rooftops with lit windows, low fog and drifting embers. The pictures are painted art in
 * public/art (made by scripts/art/make_art.py); the fog and embers drift on their own in CSS (see
 * globals.css), and each layer moves at its own speed as the page scrolls (ParallaxLayer, Motion).
 */

import { ParallaxLayer } from "@/components/parallax-layer";

// A small seeded random, so the embers are the same on every build.
function rng(seed: number) {
  return () => {
    seed = (seed * 1664525 + 1013904223) % 4294967296;
    return seed / 4294967296;
  };
}

const embers = Array.from({ length: 22 }, (_, i) => {
  const r = rng(i * 97 + 13);
  return {
    left: `${(r() * 100).toFixed(1)}%`,
    size: `${(1.5 + r() * 2.5).toFixed(1)}px`,
    delay: `${(-r() * 24).toFixed(1)}s`,
    duration: `${(16 + r() * 18).toFixed(1)}s`,
    drift: `${((r() - 0.5) * 120).toFixed(0)}px`,
  };
});

export function Backdrop() {
  return (
    <div aria-hidden className="backdrop">
      <ParallaxLayer depth={6}>
        <div className="backdrop-moon" />
      </ParallaxLayer>
      <ParallaxLayer depth={9}>
        <div className="backdrop-fog backdrop-fog-high" />
      </ParallaxLayer>
      <ParallaxLayer depth={3}>
        <div className="backdrop-city backdrop-city-far" />
      </ParallaxLayer>
      <ParallaxLayer depth={12}>
        <div className="backdrop-city backdrop-city-near" />
      </ParallaxLayer>
      <ParallaxLayer depth={16}>
        <div className="backdrop-fog backdrop-fog-low" />
      </ParallaxLayer>
      <ParallaxLayer depth={-10}>
        <div className="backdrop-embers">
          {embers.map((e, i) => (
            <span
              key={i}
              style={
                {
                  left: e.left,
                  width: e.size,
                  height: e.size,
                  animationDelay: e.delay,
                  animationDuration: e.duration,
                  "--drift": e.drift,
                } as React.CSSProperties
              }
            />
          ))}
        </div>
      </ParallaxLayer>
    </div>
  );
}
