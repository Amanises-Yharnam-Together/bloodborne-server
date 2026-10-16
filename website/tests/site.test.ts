import { describe, expect, it } from "vitest";

import { resolveSiteUrl, resolveTrailerId } from "@/lib/site";

describe("resolveSiteUrl", () => {
  it("prefers NEXT_PUBLIC_SITE_URL and drops a trailing slash", () => {
    expect(
      resolveSiteUrl({
        NEXT_PUBLIC_SITE_URL: "https://hunt.example/",
        VERCEL_PROJECT_PRODUCTION_URL: "x.vercel.app",
      }),
    ).toBe("https://hunt.example");
  });
  it("adds https to a bare host", () => {
    expect(resolveSiteUrl({ NEXT_PUBLIC_SITE_URL: "hunt.example" })).toBe("https://hunt.example");
  });
  it("falls back to Vercel's production domain, then localhost", () => {
    expect(resolveSiteUrl({ VERCEL_PROJECT_PRODUCTION_URL: "bb.vercel.app" })).toBe(
      "https://bb.vercel.app",
    );
    expect(resolveSiteUrl({})).toBe("http://localhost:3000");
  });
});

describe("resolveTrailerId", () => {
  it("accepts an id or any YouTube URL", () => {
    const id = "dQw4w9WgXcQ";
    for (const v of [
      id,
      `https://www.youtube.com/watch?v=${id}&t=3`,
      `https://youtu.be/${id}`,
      `https://www.youtube.com/embed/${id}`,
    ]) {
      expect(resolveTrailerId({ NEXT_PUBLIC_TRAILER_YOUTUBE_ID: v })).toBe(id);
    }
  });
  it("is null when unset or not a video id", () => {
    expect(resolveTrailerId({})).toBeNull();
    expect(resolveTrailerId({ NEXT_PUBLIC_TRAILER_YOUTUBE_ID: "<script>" })).toBeNull();
  });
});
