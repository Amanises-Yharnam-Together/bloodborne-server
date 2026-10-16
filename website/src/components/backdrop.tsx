/*
 * The page's backdrop, behind everything: a pale moon in the mist, two layers of a gothic city's
 * roofs and spires against a faint glow, slow fog and drifting embers. All of it is drawn here;
 * the fog and embers drift on their own in CSS (see globals.css), and each layer moves at its own
 * speed as the page scrolls (ParallaxLayer, with Motion).
 */

import { ParallaxLayer } from "@/components/parallax-layer";

// A small seeded random, so the skyline is the same on every build.
function rng(seed: number) {
  return () => {
    seed = (seed * 1664525 + 1013904223) % 4294967296;
    return seed / 4294967296;
  };
}

/** A row of roofs, towers and spires across a 1600-wide strip, as one closed path. */
function skyline(seed: number, base: number, height: number) {
  const r = rng(seed);
  const d: string[] = [`M0 ${base}`];
  let x = 0;
  while (x < 1600) {
    const kind = r();
    const w = 24 + r() * 60;
    const h = height * (0.25 + r() * 0.45);
    const top = base - h;
    if (kind < 0.26) {
      // A spire: a tall shaft and a needle roof, with pinnacles at its shoulders.
      const sw = 14 + r() * 14;
      const st = base - height * (0.75 + r() * 0.25);
      d.push(`V${top + 20} H${x + 4} V${top + 8} H${x + 8} V${top + 20}`);
      d.push(
        `H${x + w / 2 - sw / 2} V${st + 40} L${x + w / 2} ${st} L${x + w / 2 + sw / 2} ${st + 40}`,
      );
      d.push(`V${top + 20} H${x + w - 8} V${top + 8} H${x + w - 4} V${top + 20} H${x + w}`);
    } else if (kind < 0.45) {
      // A steep gabled roof.
      d.push(`V${top} L${x + w / 2} ${top - w * 0.7} L${x + w} ${top}`);
    } else if (kind < 0.6) {
      // A tower with a small pointed cap.
      d.push(`V${top} H${x + w * 0.3} L${x + w / 2} ${top - 26} L${x + w * 0.7} ${top} H${x + w}`);
    } else {
      // A flat run of houses with chimneys.
      d.push(`V${top} H${x + w * 0.35} V${top - 10} H${x + w * 0.42} V${top} H${x + w}`);
    }
    x += w;
  }
  d.push(`V${base + 400} H0 Z`);
  return d.join(" ");
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
        <svg
          className="backdrop-city backdrop-city-far"
          viewBox="0 0 1600 400"
          preserveAspectRatio="xMidYMax slice"
        >
          <path d={skyline(7, 300, 200)} />
        </svg>
      </ParallaxLayer>
      <ParallaxLayer depth={12}>
        <svg
          className="backdrop-city backdrop-city-near"
          viewBox="0 0 1600 400"
          preserveAspectRatio="xMidYMax slice"
        >
          <path d={skyline(29, 360, 240)} />
        </svg>
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
