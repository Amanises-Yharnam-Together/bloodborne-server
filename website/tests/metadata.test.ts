import { describe, expect, it } from "vitest";

import { pageMetadata } from "@/lib/metadata";

describe("pageMetadata", () => {
  it("gives every page the link card's image, site name and card type", () => {
    const m = pageMetadata({ title: "Download", description: "d", path: "/download" });
    expect(m.alternates?.canonical).toBe("/download");
    const og = m.openGraph as { siteName?: string; images?: { url: string }[] };
    expect(og.siteName).toBe("Yharnam Together: Bloodborne Seamless Co-op");
    expect(og.images?.[0].url).toBe("/opengraph-image");
    const tw = m.twitter as { card?: string; images?: { url: string }[] };
    expect(tw.card).toBe("summary_large_image");
    expect(tw.images?.[0].url).toBe("/opengraph-image");
  });
});
