// Everything the site says about itself in one place: names, links and the deployment's URL.

export const REPO = "lKaniel/bloodborne-server";
export const REPO_URL = `https://github.com/${REPO}`;
export const RELEASES_URL = `${REPO_URL}/releases`;
export const LATEST_RELEASE_URL = `${RELEASES_URL}/latest`;
export const ISSUES_URL = `${REPO_URL}/issues`;
export const blob = (path: string) => `${REPO_URL}/blob/main/${path}`;

export const site = {
  name: "Bloodborne Seamless Co-op",
  shortName: "Seamless Co-op",
  tagline: "Play all of Bloodborne with your friends, in one world.",
  description:
    "Free, open-source seamless co-op for Bloodborne on PC. Up to four players share one world on a native port of the PS4 game, connected through a small server one of them runs.",
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

// Where the server netcode stands. It is in main but not yet in a public release, which still
// carries the first, peer-to-peer design; flip this when a release ships it.
export const netcodeStatus = {
  shippedInRelease: false,
  label: "In testing",
  note: "The guide follows the next release, with the server netcode. Today's release (0.1.4) is the older peer-to-peer version: its Co-op card also asks for an account and a party password, and there is no team screen, so everyone joins the host a few seconds after loading.",
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
