// Makes every icon the site ships from assets-src/logo.png, the logo with a transparent
// background: the logo the pages show, the favicon set, the Apple touch icon and the web app
// manifest's icons. public/logo.png is also what the launcher uses. Run `npm run icons` after
// changing the logo.
import { writeFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import pngToIco from "png-to-ico";
import sharp from "sharp";

const root = fileURLToPath(new URL("..", import.meta.url));
const BG = "#0d0f13";

// The logo cropped to its edges, so every size is filled by the logo itself.
const logo = await sharp(`${root}assets-src/logo.png`).trim({ threshold: 1 }).png().toBuffer();

const square = (size) =>
  sharp(logo)
    .resize(size, size, { fit: "contain", background: { r: 0, g: 0, b: 0, alpha: 0 } })
    .png()
    .toBuffer();

// The logo on a square of the site's background, scaled to `scale` of it.
async function onBackground(size, scale = 0.9) {
  const inner = Math.round(size * scale);
  return sharp({ create: { width: size, height: size, channels: 4, background: BG } })
    .composite([{ input: await square(inner), gravity: "centre" }])
    .png()
    .toBuffer();
}

await writeFile(
  `${root}public/logo.png`,
  await sharp(await square(1024))
    .png({ palette: true, quality: 95, compressionLevel: 9 })
    .toBuffer(),
);
await writeFile(
  `${root}public/logo.webp`,
  await sharp(await square(512))
    .webp({ quality: 90 })
    .toBuffer(),
);
await writeFile(`${root}src/app/icon.png`, await square(96));
await writeFile(
  `${root}src/app/favicon.ico`,
  await pngToIco(await Promise.all([16, 32, 48].map((s) => square(s)))),
);
await writeFile(`${root}src/app/apple-icon.png`, await onBackground(180));
await writeFile(`${root}public/icon-192.png`, await onBackground(192));
await writeFile(`${root}public/icon-512.png`, await onBackground(512));
await writeFile(`${root}public/icon-maskable-512.png`, await onBackground(512, 0.72));
console.log("icons written");
