// Makes src/assets/og-backdrop.png, the picture behind the social preview (src/app/opengraph-image.tsx):
// the site's own backdrop at 1200x630, the mist from above, the pale moon, the far city in the
// fog, the near rooftops with lit windows and the low fog, from the art in public/art. The preview
// draws the logo and the name over it. Run `npm run og` after changing the art.
import { writeFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import sharp from "sharp";

const root = fileURLToPath(new URL("..", import.meta.url));
const art = (name) => `${root}public/art/${name}.webp`;
const W = 1200;
const H = 630;
const BG = "#0d0f13";

/** An art layer scaled to `width` (then cut `left` px in from its left edge to the canvas's
 *  width), with its alpha multiplied by `opacity`. */
async function layer(name, width, opacity = 1, left = 0) {
  const scaled = await sharp(art(name)).resize({ width }).toBuffer({ resolveWithObject: true });
  const cut = sharp(scaled.data).extract({
    left,
    top: 0,
    width: Math.min(W, scaled.info.width - left),
    height: scaled.info.height,
  });
  const resized = await cut.ensureAlpha().raw().toBuffer({ resolveWithObject: true });
  const { data, info } = resized;
  for (let i = 3; i < data.length; i += 4) data[i] = Math.round(data[i] * opacity);
  return sharp(data, { raw: info })
    .png()
    .toBuffer()
    .then(async (input) => ({
      input,
      height: info.height,
    }));
}

// The same light as the page's body: mist from above and a dark vignette.
const light = Buffer.from(`<svg xmlns="http://www.w3.org/2000/svg" width="${W}" height="${H}">
  <defs>
    <radialGradient id="mist" cx="50%" cy="-5%" r="70%" gradientTransform="translate(0.5 0) scale(1 0.9) translate(-0.5 0)">
      <stop offset="0" stop-color="rgb(120,138,168)" stop-opacity="0.12"/>
      <stop offset="1" stop-color="rgb(120,138,168)" stop-opacity="0"/>
    </radialGradient>
    <radialGradient id="vignette" cx="50%" cy="50%" r="75%">
      <stop offset="0.55" stop-color="#000" stop-opacity="0"/>
      <stop offset="1" stop-color="#000" stop-opacity="0.8"/>
    </radialGradient>
  </defs>
  <rect width="100%" height="100%" fill="url(#mist)"/>
  <rect width="100%" height="100%" fill="url(#vignette)"/>
</svg>`);

const moon = await layer("moon", 210, 0.28);
const far = await layer("skyline-far", W, 0.75);
const near = await layer("skyline-near", W, 1);
const fog = await layer("fog", Math.round(W * 1.3), 0.5, 180);

const png = await sharp({ create: { width: W, height: H, channels: 4, background: BG } })
  .composite([
    { input: moon.input, top: 14, left: W - 210 - 26 },
    { input: far.input, top: H - far.height + 10, left: 0 },
    { input: near.input, top: H - near.height + 25, left: 0 },
    { input: fog.input, top: H - fog.height + 120, left: 0 },
    { input: light, top: 0, left: 0 },
  ])
  .flatten({ background: BG })
  .png({ compressionLevel: 9 })
  .toBuffer();

await writeFile(`${root}src/assets/og-backdrop.png`, png);
console.log(`og-backdrop.png written (${Math.round(png.length / 1024)} KB)`);
