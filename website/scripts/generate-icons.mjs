// Draws every icon the site ships from src/assets/mark.svg: the favicon set, the Apple touch
// icon and the web app manifest's icons. Run `npm run icons` after changing the mark.
import { readFile, writeFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import pngToIco from "png-to-ico";
import sharp from "sharp";

const root = fileURLToPath(new URL("..", import.meta.url));
const BG = "#0a0908";
const mark = await readFile(`${root}src/assets/mark.svg`, "utf8");
const inner = mark.replace(/^[\s\S]*?<svg[^>]*>/, "").replace(/<\/svg>\s*$/, "");

// The mark on the site's background, scaled to `scale` of the canvas; `radius` rounds the
// corners (in a 64 unit box).
function onBackground({ scale = 0.86, radius = 0 } = {}) {
  const offset = (64 * (1 - scale)) / 2;
  return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64">
  <rect width="64" height="64" rx="${radius}" fill="${BG}"/>
  <rect x="2.5" y="2.5" width="59" height="59" rx="${Math.max(radius - 2.5, 0)}" fill="none" stroke="#b89c66" stroke-opacity="0.35"/>
  <g transform="translate(${offset} ${offset}) scale(${scale})">${inner}</g>
</svg>
`;
}

const png = (svg, size) =>
  sharp(Buffer.from(svg), { density: Math.max(72, (size / 64) * 72 * 2) })
    .resize(size, size)
    .png()
    .toBuffer();

const favicon = onBackground({ scale: 0.92, radius: 12 });
await writeFile(`${root}src/app/icon.svg`, favicon);
await writeFile(
  `${root}src/app/favicon.ico`,
  await pngToIco(await Promise.all([16, 32, 48].map((s) => png(favicon, s)))),
);
await writeFile(`${root}src/app/apple-icon.png`, await png(onBackground({ scale: 0.8 }), 180));
await writeFile(`${root}public/icon-192.png`, await png(onBackground({ radius: 12 }), 192));
await writeFile(`${root}public/icon-512.png`, await png(onBackground({ radius: 12 }), 512));
await writeFile(
  `${root}public/icon-maskable-512.png`,
  await png(onBackground({ scale: 0.62 }), 512),
);
console.log("icons written");
