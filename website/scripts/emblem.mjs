// The site's emblem, drawn in code: a hunter's bell under a gothic arch, its clapper a drop of
// blood and its call drawn as rings of sound. The bell stands for calling a friend into your
// world. Our own drawing, no game art. `emblem()` is the detailed version for large sizes;
// `mark()` keeps only the bell for favicons.

const f = (n) => Number(n.toFixed(2));

const defs = `
  <linearGradient id="silver" x1="0" y1="0" x2="0" y2="1">
    <stop offset="0" stop-color="#f2ede2"/><stop offset="0.55" stop-color="#bdb6a8"/><stop offset="1" stop-color="#6f6a61"/>
  </linearGradient>
  <linearGradient id="gold" x1="0" y1="0" x2="1" y2="1">
    <stop offset="0" stop-color="#ecd49a"/><stop offset="0.5" stop-color="#b08a4c"/><stop offset="1" stop-color="#6b4f25"/>
  </linearGradient>
  <radialGradient id="field" cx="0.5" cy="0.45" r="0.65">
    <stop offset="0" stop-color="#2a241d"/><stop offset="0.6" stop-color="#110f0d"/><stop offset="1" stop-color="#060505"/>
  </radialGradient>
  <linearGradient id="bronze" x1="0" y1="0" x2="1" y2="0">
    <stop offset="0" stop-color="#1e170e"/><stop offset="0.25" stop-color="#7d6a49"/><stop offset="0.42" stop-color="#d8c8a2"/>
    <stop offset="0.62" stop-color="#7a6644"/><stop offset="1" stop-color="#16110a"/>
  </linearGradient>
  <linearGradient id="blood" x1="0.3" y1="0" x2="0.7" y2="1">
    <stop offset="0" stop-color="#d63c33"/><stop offset="0.55" stop-color="#8e1a17"/><stop offset="1" stop-color="#4a0907"/>
  </linearGradient>
  <radialGradient id="halo" cx="0.5" cy="0.5" r="0.5">
    <stop offset="0" stop-color="#e9c98a" stop-opacity="0.32"/><stop offset="1" stop-color="#e9c98a" stop-opacity="0"/>
  </radialGradient>`;

const BELL =
  "M256 150C214 150 197 181 195 222C193 263 187 300 169 329C161 342 146 349 146 360H366C366 349 351 342 343 329C325 300 319 263 317 222C315 181 298 150 256 150Z";
const DROP = "M256 376c0 0-22 30-22 47a22 22 0 0 0 44 0c0-17-22-47-22-47z";

function bell({ hatching = true, size = 1 } = {}) {
  const scale = `translate(256 280) scale(${size}) translate(-256 -280)`;
  const hatch = [];
  if (hatching) {
    for (let i = 0; i < 12; i++) {
      const x = 268 + i * 9;
      hatch.push(`<path d="M${f(x - 6)} 150C${f(x + 2)} 230 ${f(x + 10)} 300 ${f(x + 24)} 362"/>`);
    }
  }
  return `
  <circle cx="256" cy="270" r="150" fill="url(#halo)"/>
  <g transform="${scale}">
  <path d="M240 152V134a16 16 0 0 1 32 0V152" fill="none" stroke="url(#bronze)" stroke-width="9"/>
  <path d="${BELL}" fill="url(#bronze)" stroke="#2a1f10" stroke-width="3"/>
  <clipPath id="bellclip"><path d="${BELL}"/></clipPath>
  <g clip-path="url(#bellclip)">
    <g fill="none" stroke="#120d07" stroke-opacity="0.45" stroke-width="1.2">${hatch.join("")}</g>
    <path d="M190 246C230 238 282 238 322 246" fill="none" stroke="#3a2c18" stroke-width="3"/>
    <path d="M188 258C230 250 282 250 324 258" fill="none" stroke="#f5e6bf" stroke-opacity="0.45" stroke-width="1.5"/>
    <path d="M176 318C228 308 284 308 336 318" fill="none" stroke="#3a2c18" stroke-width="3"/>
    <path d="M222 168C210 196 208 260 196 318" fill="none" stroke="#fff6dc" stroke-opacity="0.5" stroke-width="5" stroke-linecap="round"/>
  </g>
  <rect x="140" y="356" width="232" height="14" rx="7" fill="url(#bronze)" stroke="#2a1f10" stroke-width="3"/>
  <path d="M256 370v8" stroke="#3a2c18" stroke-width="4"/>
  <path d="${DROP}" fill="url(#blood)" stroke="#2a0504" stroke-width="2"/>
  <path d="M247 414c-2 6-2 12 0 16" fill="none" stroke="#f3b3a4" stroke-opacity="0.75" stroke-width="4" stroke-linecap="round"/>
  </g>`;
}

// Rings of sound to each side of the bell.
function rings() {
  const arcs = [];
  for (const [r, o] of [
    [96, 0.9],
    [108, 0.6],
    [120, 0.35],
  ]) {
    for (const side of [-1, 1]) {
      const a0 = (side < 0 ? 196 : -16) * (Math.PI / 180);
      const a1 = (side < 0 ? 164 : 16) * (Math.PI / 180);
      const p = (a) => `${f(256 + r * Math.cos(a))} ${f(262 + r * Math.sin(a))}`;
      arcs.push(
        `<path d="M${p(a0)}A${r} ${r} 0 0 ${side < 0 ? 0 : 1} ${p(a1)}" stroke-opacity="${o}"/>`,
      );
    }
  }
  return `<g fill="none" stroke="url(#gold)" stroke-width="2.5" stroke-linecap="round">${arcs.join("")}</g>`;
}

const ARCH = "M116 474V240A220 220 0 0 1 256 35A220 220 0 0 1 396 240V474Z";
const INNER = "M130 460V242A206 206 0 0 1 256 53A206 206 0 0 1 382 242V460Z";

function trefoil(cx, cy, r) {
  return `<g fill="none" stroke="url(#silver)" stroke-width="2">
    <circle cx="${cx}" cy="${cy - r * 0.55}" r="${r * 0.5}"/>
    <circle cx="${f(cx - r * 0.5)}" cy="${f(cy + r * 0.3)}" r="${r * 0.5}"/>
    <circle cx="${f(cx + r * 0.5)}" cy="${f(cy + r * 0.3)}" r="${r * 0.5}"/>
    <circle cx="${cx}" cy="${cy}" r="${r * 1.15}" stroke-width="1.5"/>
  </g>`;
}

function star(x, y, r) {
  return `<path d="M${x} ${y - r}L${f(x + r * 0.22)} ${f(y - r * 0.22)}L${x + r} ${y}L${f(x + r * 0.22)} ${f(y + r * 0.22)}L${x} ${y + r}L${f(x - r * 0.22)} ${f(y + r * 0.22)}L${x - r} ${y}L${f(x - r * 0.22)} ${f(y - r * 0.22)}z" fill="#e8e2d4"/>`;
}

export function emblem() {
  // Crockets: small gold leaves up both sides of the arch.
  const crockets = [];
  for (let i = 1; i <= 5; i++) {
    const t = (i / 6) * (Math.PI / 2 - 0.05);
    for (const side of [-1, 1]) {
      const cx = side < 0 ? 336 : 176;
      const a = side < 0 ? Math.PI + t : -t;
      const x = f(cx + 226 * Math.cos(a));
      const y = f(240 + 226 * Math.sin(a));
      crockets.push(`<circle cx="${x}" cy="${y}" r="3.5"/>`);
    }
  }
  return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 512 512">
  <defs>${defs}</defs>
  <path d="${ARCH}" fill="url(#field)" stroke="url(#silver)" stroke-width="5" stroke-linejoin="round"/>
  <path d="${INNER}" fill="none" stroke="#bdb6a8" stroke-opacity="0.55" stroke-width="1.5"/>
  <g fill="url(#gold)">${crockets.join("")}</g>
  <path d="M256 12l9 14-9 14-9-14z" fill="url(#gold)"/>
  <path d="M256 40v-4" stroke="url(#gold)" stroke-width="3"/>
  ${trefoil(256, 98, 18)}
  ${star(186, 150, 6)}${star(330, 136, 8)}${star(352, 182, 4)}${star(166, 196, 3.5)}
  ${rings()}
  ${bell({ size: 0.74 })}
  <rect x="100" y="466" width="312" height="22" fill="#0b0a09" stroke="url(#silver)" stroke-width="3"/>
  <path d="M120 477h108M284 477h108" stroke="url(#gold)" stroke-width="1.5"/>
  <path d="M256 469l8 8-8 8-8-8z" fill="url(#gold)"/>
</svg>
`;
}

export function mark() {
  return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="120 104 272 340">
  <defs>${defs}</defs>
  ${bell({ hatching: false })}
</svg>
`;
}
