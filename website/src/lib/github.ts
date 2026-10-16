import { cacheLife } from "next/cache";

import { REPO } from "@/lib/site";

export type ReleaseAsset = {
  name: string;
  url: string;
  size: number;
  checksumUrl: string | null;
};

export type Release = {
  tag: string;
  name: string;
  url: string;
  publishedAt: string;
  assets: ReleaseAsset[];
};

type GitHubAsset = { name: string; browser_download_url: string; size: number };
type GitHubRelease = {
  tag_name: string;
  name: string | null;
  html_url: string;
  published_at: string | null;
  draft: boolean;
  prerelease: boolean;
  assets: GitHubAsset[];
};

/** Turns GitHub's release JSON into what the download page shows: installers first, with their
 *  .sha256 files attached to them rather than listed as downloads of their own. */
export function parseRelease(json: unknown): Release | null {
  const r = json as Partial<GitHubRelease> | null;
  if (!r || typeof r.tag_name !== "string" || typeof r.html_url !== "string") return null;
  if (r.draft) return null;
  const all = Array.isArray(r.assets) ? r.assets : [];
  const checksums = new Map(
    all.filter((a) => a.name.endsWith(".sha256")).map((a) => [a.name, a.browser_download_url]),
  );
  const assets = all
    .filter((a) => !a.name.endsWith(".sha256"))
    .map((a) => ({
      name: a.name,
      url: a.browser_download_url,
      size: a.size,
      checksumUrl: checksums.get(`${a.name}.sha256`) ?? null,
    }))
    .sort((a, b) => rank(a.name) - rank(b.name));
  return {
    tag: r.tag_name,
    name: r.name?.trim() || r.tag_name,
    url: r.html_url,
    publishedAt: r.published_at ?? "",
    assets,
  };
}

function rank(name: string): number {
  if (name.endsWith(".exe")) return 0;
  if (name.endsWith(".zip")) return 1;
  return 2;
}

export function formatBytes(bytes: number): string {
  if (!Number.isFinite(bytes) || bytes <= 0) return "";
  const units = ["B", "KB", "MB", "GB"];
  let value = bytes;
  let unit = 0;
  while (value >= 1024 && unit < units.length - 1) {
    value /= 1024;
    unit++;
  }
  return `${value >= 10 || unit === 0 ? Math.round(value) : value.toFixed(1)} ${units[unit]}`;
}

/** The latest published release, cached for hours. Null when GitHub cannot be reached; the
 *  pages then link to the releases page instead. */
export async function getLatestRelease(): Promise<Release | null> {
  "use cache";
  cacheLife("hours");
  try {
    const headers: Record<string, string> = {
      Accept: "application/vnd.github+json",
      "X-GitHub-Api-Version": "2022-11-28",
    };
    if (process.env.GITHUB_TOKEN) headers.Authorization = `Bearer ${process.env.GITHUB_TOKEN}`;
    const res = await fetch(`https://api.github.com/repos/${REPO}/releases/latest`, { headers });
    if (!res.ok) return null;
    return parseRelease(await res.json());
  } catch {
    return null;
  }
}
