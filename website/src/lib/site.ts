// Everything the site says about itself in one place: names, links and the deployment's URL.

export const REPO = "lKaniel/bloodborne-server";
export const REPO_URL = `https://github.com/${REPO}`;
export const RELEASES_URL = `${REPO_URL}/releases`;
export const LATEST_RELEASE_URL = `${RELEASES_URL}/latest`;
export const ISSUES_URL = `${REPO_URL}/issues`;
// The server rewrite is developed on its own branch until its last phase is verified.
export const SERVER_COOP_BRANCH_URL = `${REPO_URL}/tree/server-coop`;

export const site = {
  name: "Bloodborne Seamless Co-op",
  shortName: "Seamless Co-op",
  tagline: "Hunt Yharnam together. No bells, no signs, no peer-to-peer.",
  description:
    "Free, open-source seamless co-op for Bloodborne on PC: a native port of the PS4 game tuned for co-op, with a rewritten server-based netcode. Up to four hunters share one world and stay together through lamps, deaths and bosses.",
  keywords: [
    "Bloodborne",
    "Bloodborne PC",
    "Bloodborne seamless co-op",
    "Bloodborne co-op mod",
    "Bloodborne multiplayer",
    "Bloodborne PC port",
    "seamless co-op",
    "self-hosted game server",
    "open source",
  ],
  license: "GPL-2.0-or-later",
} as const;

// Where the server rewrite stands. The public releases still carry the first, peer-to-peer
// design; flip this when a release ships the server netcode.
export const netcodeStatus = {
  shippedInRelease: false,
  label: "In testing",
  note: "The server netcode is complete on the server-coop branch and in testing; it ships in the next public release. Today's release uses the first, peer-to-peer design.",
} as const;

type Env = Record<string, string | undefined>;

/**
 * The deployment's public origin, without a trailing slash. NEXT_PUBLIC_SITE_URL wins (set it to
 * the real domain); on Vercel the production domain is the fallback, then localhost.
 */
export function resolveSiteUrl(env: Env = process.env): string {
  const explicit = env.NEXT_PUBLIC_SITE_URL?.trim();
  if (explicit) return withProtocol(explicit);
  const vercel = env.VERCEL_PROJECT_PRODUCTION_URL?.trim();
  if (vercel) return withProtocol(vercel);
  return "http://localhost:3000";
}

function withProtocol(host: string): string {
  const url = /^https?:\/\//.test(host) ? host : `https://${host}`;
  return url.replace(/\/+$/, "");
}

export const siteUrl = resolveSiteUrl();

export function absoluteUrl(path = "/"): string {
  return new URL(path, `${siteUrl}/`).toString();
}

/**
 * The trailer. Set NEXT_PUBLIC_TRAILER_YOUTUBE_ID to a YouTube video id (or a full YouTube URL)
 * and the landing page embeds it; unset, the page shows a framed placeholder.
 */
export function resolveTrailerId(env: Env = process.env): string | null {
  const raw = env.NEXT_PUBLIC_TRAILER_YOUTUBE_ID?.trim();
  if (!raw) return null;
  const fromUrl = raw.match(/(?:youtu\.be\/|[?&]v=|\/embed\/|\/shorts\/)([\w-]{11})/);
  const id = fromUrl ? fromUrl[1] : raw;
  return /^[\w-]{11}$/.test(id) ? id : null;
}
