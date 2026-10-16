import { readFile } from "node:fs/promises";
import { join } from "node:path";
import { ImageResponse } from "next/og";

import { site } from "@/lib/site";

// The social preview: the emblem and the name, set like the game's title screen.
export const alt = `${site.name}: play Bloodborne together on PC`;
export const size = { width: 1200, height: 630 };
export const contentType = "image/png";

const fonts = join(process.cwd(), "src/assets/fonts");
const regular = await readFile(join(fonts, "EBGaramond-Regular.ttf"));
const italic = await readFile(join(fonts, "EBGaramond-Italic.ttf"));
const emblem = `data:image/svg+xml;base64,${(await readFile(join(process.cwd(), "src/assets/emblem.svg"))).toString("base64")}`;

export default function Image() {
  return new ImageResponse(
    <div
      style={{
        width: "100%",
        height: "100%",
        display: "flex",
        alignItems: "center",
        justifyContent: "center",
        gap: 64,
        background:
          "radial-gradient(ellipse 80% 70% at 50% 0%, rgba(190,186,176,0.13), rgba(5,5,5,1) 70%), #050505",
        color: "#f3ead8",
        fontFamily: "Garamond",
      }}
    >
      <img src={emblem} width={300} height={300} alt="" />
      <div style={{ display: "flex", flexDirection: "column" }}>
        <div style={{ fontSize: 92, lineHeight: 1 }}>Bloodborne</div>
        <div style={{ fontSize: 54, color: "#c4a46b", marginTop: 8 }}>Seamless Co-op</div>
        <div
          style={{
            fontSize: 30,
            color: "#b9b4aa",
            marginTop: 30,
            fontStyle: "italic",
            maxWidth: 560,
          }}
        >
          {site.tagline}
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
