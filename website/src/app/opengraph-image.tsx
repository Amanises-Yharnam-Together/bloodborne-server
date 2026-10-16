import { readFile } from "node:fs/promises";
import { join } from "node:path";
import { ImageResponse } from "next/og";

import { site } from "@/lib/site";

// The social preview: the mark, the name and the tagline in the site's own type.
export const alt = `${site.name}: play Bloodborne together on PC`;
export const size = { width: 1200, height: 630 };
export const contentType = "image/png";

const fonts = join(process.cwd(), "src/assets/fonts");
const cinzel = await readFile(join(fonts, "Cinzel-Medium.ttf"));
const garamond = await readFile(join(fonts, "EBGaramond-Italic.ttf"));
const mark = `data:image/svg+xml;base64,${(await readFile(join(process.cwd(), "src/assets/mark.svg"))).toString("base64")}`;

export default function Image() {
  return new ImageResponse(
    <div
      style={{
        width: "100%",
        height: "100%",
        display: "flex",
        flexDirection: "column",
        alignItems: "center",
        justifyContent: "center",
        background:
          "radial-gradient(ellipse 70% 60% at 50% 0%, rgba(232,220,196,0.11), rgba(10,9,8,1) 70%), #0a0908",
        color: "#f1e9d8",
        border: "2px solid rgba(184,156,102,0.45)",
      }}
    >
      <img src={mark} width={150} height={150} alt="" />
      <div
        style={{
          fontFamily: "Cinzel",
          fontSize: 86,
          letterSpacing: 4,
          marginTop: 24,
        }}
      >
        Bloodborne
      </div>
      <div
        style={{
          fontFamily: "Cinzel",
          fontSize: 40,
          letterSpacing: 14,
          color: "#b89c66",
          marginTop: 4,
        }}
      >
        SEAMLESS CO-OP
      </div>
      <div
        style={{
          fontFamily: "Garamond",
          fontSize: 36,
          color: "#ddd4c2",
          marginTop: 34,
          fontStyle: "italic",
        }}
      >
        {site.tagline}
      </div>
    </div>,
    {
      ...size,
      fonts: [
        { name: "Cinzel", data: cinzel, weight: 500, style: "normal" },
        { name: "Garamond", data: garamond, weight: 400, style: "italic" },
      ],
    },
  );
}
