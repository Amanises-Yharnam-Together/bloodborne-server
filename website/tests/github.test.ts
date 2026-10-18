import { describe, expect, it } from "vitest";

import { formatBytes, newestRelease, parseRelease } from "@/lib/github";

const asset = (name: string, size = 100) => ({
  name,
  size,
  browser_download_url: `https://github.com/x/releases/download/v1/${name}`,
});

describe("parseRelease", () => {
  it("puts the installer first and attaches its checksum", () => {
    const r = parseRelease({
      tag_name: "v0.1.4",
      name: "Bloodborne Seamless Co-op v0.1.4",
      html_url: "https://github.com/x/releases/tag/v0.1.4",
      published_at: "2026-10-14T02:17:25Z",
      draft: false,
      prerelease: false,
      assets: [
        asset("notes.txt"),
        asset("BB-v0.1.4-setup.exe.sha256"),
        asset("BB-v0.1.4-setup.exe", 5e8),
      ],
    });
    expect(r?.tag).toBe("v0.1.4");
    expect(r?.assets.map((a) => a.name)).toEqual(["BB-v0.1.4-setup.exe", "notes.txt"]);
    expect(r?.assets[0].checksumUrl).toMatch(/setup\.exe\.sha256$/);
    expect(r?.assets[1].checksumUrl).toBeNull();
  });
  it("rejects drafts and garbage", () => {
    expect(parseRelease({ tag_name: "v1", html_url: "u", draft: true, assets: [] })).toBeNull();
    expect(parseRelease(null)).toBeNull();
    expect(parseRelease({ message: "Not Found" })).toBeNull();
  });
  it("names an unnamed release by its tag", () => {
    expect(parseRelease({ tag_name: "v2", name: "", html_url: "u", assets: [] })?.name).toBe("v2");
  });
});

describe("newestRelease", () => {
  const rel = (tag: string, extra: object = {}) => ({
    tag_name: tag,
    html_url: `u/${tag}`,
    assets: [],
    ...extra,
  });
  it("takes the newest published release, pre-releases included", () => {
    const r = newestRelease([
      rel("v0.3.13-local", { draft: true }),
      rel("v0.3.12-local", { prerelease: true }),
      rel("v0.1.4"),
    ]);
    expect(r?.tag).toBe("v0.3.12-local");
    expect(r?.prerelease).toBe(true);
  });
  it("is null for an empty or broken list", () => {
    expect(newestRelease([])).toBeNull();
    expect(newestRelease({ message: "Not Found" })).toBeNull();
  });
});

describe("formatBytes", () => {
  it("formats sizes", () => {
    expect(formatBytes(512)).toBe("512 B");
    expect(formatBytes(1536)).toBe("1.5 KB");
    expect(formatBytes(487_000_000)).toBe("464 MB");
    expect(formatBytes(0)).toBe("");
  });
});
