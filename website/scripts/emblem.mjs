// The site's emblem, drawn in code: an engraved medallion with a ring of ticks and scrollwork,
// a hatched crescent moon and a drop of blood. Our own drawing, no game art. `emblem()` is the
// detailed version for large sizes; `mark()` keeps only the moon and the drop for favicons.

const C = 256;
const f = (n) => Number(n.toFixed(2));
const polar = (r, deg) => {
  const a = ((deg - 90) * Math.PI) / 180;
  return [f(C + r * Math.cos(a)), f(C + r * Math.sin(a))];
};

const defs = `
  <linearGradient id="silver" x1="0" y1="0" x2="0" y2="1">
    <stop offset="0" stop-color="#f2ede2"/><stop offset="0.55" stop-color="#bdb6a8"/><stop offset="1" stop-color="#6f6a61"/>
  </linearGradient>
  <linearGradient id="gold" x1="0" y1="0" x2="1" y2="1">
    <stop offset="0" stop-color="#e2c98f"/><stop offset="0.5" stop-color="#b08a4c"/><stop offset="1" stop-color="#6b4f25"/>
  </linearGradient>
  <radialGradient id="field" cx="0.45" cy="0.38" r="0.7">
    <stop offset="0" stop-color="#1f1c19"/><stop offset="1" stop-color="#070606"/>
  </radialGradient>
  <linearGradient id="moon" x1="0.1" y1="0" x2="0.7" y2="1">
    <stop offset="0" stop-color="#f6f0e2"/><stop offset="0.6" stop-color="#cfc6b2"/><stop offset="1" stop-color="#8d8573"/>
  </linearGradient>
  <linearGradient id="blood" x1="0.3" y1="0" x2="0.7" y2="1">
    <stop offset="0" stop-color="#d13a32"/><stop offset="0.55" stop-color="#8e1a17"/><stop offset="1" stop-color="#4a0907"/>
  </linearGradient>
  <mask id="crescent">
    <rect width="512" height="512" fill="#000"/>
    <circle cx="236" cy="268" r="142" fill="#fff"/>
    <circle cx="300" cy="222" r="122" fill="#000"/>
  </mask>`;

function moonAndDrop() {
  const hatch = [];
  for (let i = -40; i < 60; i++) {
    const x = i * 7;
    hatch.push(`<line x1="${x}" y1="0" x2="${x + 300}" y2="512"/>`);
  }
  return `
  <g mask="url(#crescent)">
    <circle cx="236" cy="268" r="142" fill="url(#moon)"/>
    <g stroke="#3a352e" stroke-opacity="0.32" stroke-width="1.4">${hatch.join("")}</g>
    <circle cx="190" cy="330" r="16" fill="#8d8573" fill-opacity="0.35"/>
    <circle cx="150" cy="262" r="9" fill="#8d8573" fill-opacity="0.35"/>
    <circle cx="214" cy="386" r="7" fill="#8d8573" fill-opacity="0.3"/>
  </g>
  <circle cx="236" cy="268" r="142" fill="none" stroke="#f2ede2" stroke-opacity="0.18" stroke-width="1.5" mask="url(#crescent)"/>
  <path d="M318 214c0 0-34 46-34 72a34 34 0 0 0 68 0c0-26-34-72-34-72z" fill="url(#blood)" stroke="#2a0504" stroke-width="2"/>
  <path d="M305 268c-3 9-4 18-1 24" fill="none" stroke="#f3b3a4" stroke-opacity="0.75" stroke-width="5" stroke-linecap="round"/>`;
}

function star(x, y, r) {
  return `<path d="M${x} ${y - r}L${x + r * 0.22} ${y - r * 0.22}L${x + r} ${y}L${x + r * 0.22} ${y + r * 0.22}L${x} ${y + r}L${x - r * 0.22} ${y + r * 0.22}L${x - r} ${y}L${x - r * 0.22} ${y - r * 0.22}z" fill="#e8e2d4"/>`;
}

function scroll(deg) {
  // A small mirrored scroll on the ring, pointing at the centre.
  return `<g transform="rotate(${deg} ${C} ${C})">
    <path d="M256 30c-10 0-16 7-16 14 0 6 5 10 10 10s8-4 8-8M256 30c10 0 16 7 16 14 0 6-5 10-10 10s-8-4-8-8" fill="none" stroke="url(#gold)" stroke-width="3" stroke-linecap="round"/>
    <path d="M256 22l7 10-7 10-7-10z" fill="url(#gold)"/>
    <circle cx="256" cy="62" r="3" fill="url(#gold)"/>
  </g>`;
}

export function emblem() {
  const ticks = [];
  for (let i = 0; i < 120; i++) {
    const long = i % 5 === 0;
    const [x1, y1] = polar(long ? 206 : 211, i * 3);
    const [x2, y2] = polar(219, i * 3);
    ticks.push(
      `<line x1="${x1}" y1="${y1}" x2="${x2}" y2="${y2}" stroke-width="${long ? 2.2 : 1.1}"/>`,
    );
  }
  const scrolls = [0, 90, 180, 270].map(scroll).join("");
  const dots = [45, 135, 225, 315]
    .map((d) => {
      const [x, y] = polar(226, d);
      return `<circle cx="${x}" cy="${y}" r="4.5" fill="url(#gold)"/>`;
    })
    .join("");
  return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 512 512">
  <defs>${defs}</defs>
  <circle cx="256" cy="256" r="246" fill="#050404"/>
  <circle cx="256" cy="256" r="240" fill="none" stroke="url(#silver)" stroke-width="5"/>
  <circle cx="256" cy="256" r="231" fill="none" stroke="url(#silver)" stroke-width="1.5"/>
  <g stroke="#cfc8b9" stroke-opacity="0.85">${ticks.join("")}</g>
  <circle cx="256" cy="256" r="200" fill="url(#field)" stroke="url(#silver)" stroke-width="2.5"/>
  <circle cx="256" cy="256" r="192" fill="none" stroke="#bdb6a8" stroke-opacity="0.35" stroke-width="1"/>
  ${scrolls}${dots}
  ${moonAndDrop()}
  ${star(352, 150, 9)}${star(382, 196, 5)}${star(328, 116, 4)}
</svg>
`;
}

export function mark() {
  return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="60 80 340 340">
  <defs>${defs}</defs>
  ${moonAndDrop()}
</svg>
`;
}
