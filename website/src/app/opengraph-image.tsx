import { readFile } from "node:fs/promises";
import { join } from "node:path";
import { ImageResponse } from "next/og";

import { site } from "@/lib/site";

// The social preview (link cards in chats, search and social sites): the site's own backdrop of
// moon, city and fog (src/assets/og-backdrop.png, from `npm run og`) with the logo and the name.
export const alt = `${site.shortName}: Bloodborne seamless co-op on PC`;
export const size = { width: 1200, height: 630 };
export const contentType = "image/png";

const fonts = join(process.cwd(), "src/assets/fonts");
const regular = await readFile(join(fonts, "EBGaramond-Regular.ttf"));
const italic = await readFile(join(fonts, "EBGaramond-Italic.ttf"));
const png = (data: Buffer) => `data:image/png;base64,${data.toString("base64")}`;
const backdrop = png(await readFile(join(process.cwd(), "src/assets/og-backdrop.png")));
const emblem = png(await readFile(join(process.cwd(), "public/logo.png")));

export default function Image() {
  return new ImageResponse(
    <div
      style={{
        width: "100%",
        height: "100%",
        display: "flex",
        position: "relative",
        background: "#0d0f13",
        color: "#f0e2c2",
        fontFamily: "Garamond",
      }}
    >
      <img
        src={backdrop}
        width={size.width}
        height={size.height}
        alt=""
        style={{ position: "absolute", top: 0, left: 0 }}
      />
      <div
        style={{
          display: "flex",
          alignItems: "center",
          gap: 56,
          width: "100%",
          padding: "0 80px 70px",
        }}
      >
        <img src={emblem} width={300} height={300} alt="" />
        <div style={{ display: "flex", flexDirection: "column" }}>
          <div style={{ fontSize: 92, lineHeight: 1 }}>{site.shortName}</div>
          <div style={{ fontSize: 50, color: "#d9b878", marginTop: 10 }}>
            Bloodborne Seamless Co-op
          </div>
          <div
            style={{
              width: 520,
              height: 1,
              marginTop: 28,
              background: "linear-gradient(90deg, rgba(201,168,106,0.9), rgba(201,168,106,0))",
            }}
          />
          <div
            style={{
              fontSize: 30,
              color: "#aea590",
              marginTop: 22,
              fontStyle: "italic",
              maxWidth: 700,
            }}
          >
            {site.tagline}
          </div>
        </div>
      </div>
    </div>,
    {
      ...size,
      fonts: [
        { name: "Garamond", data: regular, weight: 400, style: "normal" },
        { name: "Garamond", data: italic, weight: 400, style: "italic" },
      ],
    },
  );
}
