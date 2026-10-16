// Draws every icon the site ships from scripts/emblem.mjs: the emblem and the small mark the
// pages show, the favicon set, the Apple touch icon and the web app manifest's icons. Run
// `npm run icons` after changing the emblem.
import { writeFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import pngToIco from "png-to-ico";
import sharp from "sharp";

import { emblem, mark } from "./emblem.mjs";

const root = fileURLToPath(new URL("..", import.meta.url));
const BG = "#0d0f13";

const png = (svg, size) =>
  sharp(Buffer.from(svg), { density: Math.max(72, (size / 64) * 72 * 2) })
    .resize(size, size)
    .png()
    .toBuffer();

// An SVG drawn on a square of the site's background, scaled to `scale` of it.
function onBackground(svg, { scale = 0.9, radius = 0 } = {}) {
  const inner = Buffer.from(svg).toString("base64");
  const s = 512 * scale;
  const o = (512 - s) / 2;
  return `<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" viewBox="0 0 512 512">
  <rect width="512" height="512" rx="${radius}" fill="${BG}"/>
  <image x="${o}" y="${o}" width="${s}" height="${s}" href="data:image/svg+xml;base64,${inner}"/>
</svg>
`;
}

await writeFile(`${root}public/emblem.svg`, emblem());
await writeFile(`${root}public/mark.svg`, mark());
await writeFile(`${root}src/assets/emblem.svg`, emblem());

// Favicons keep only the moon and the drop: the ring's detail is lost below 64 pixels.
const small = onBackground(mark(), { scale: 0.96, radius: 96 });
await writeFile(`${root}src/app/icon.svg`, small);
await writeFile(
  `${root}src/app/favicon.ico`,
  await pngToIco(await Promise.all([16, 32, 48].map((s) => png(small, s)))),
);
await writeFile(`${root}src/app/apple-icon.png`, await png(onBackground(emblem()), 180));
await writeFile(`${root}public/icon-192.png`, await png(onBackground(emblem()), 192));
await writeFile(`${root}public/icon-512.png`, await png(onBackground(emblem()), 512));
await writeFile(
  `${root}public/icon-maskable-512.png`,
  await png(onBackground(emblem(), { scale: 0.7 }), 512),
);
console.log("icons written");
