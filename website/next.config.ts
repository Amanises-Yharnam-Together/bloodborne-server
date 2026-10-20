import type { NextConfig } from "next";

import { SITE_DOMAIN } from "./src/lib/site";

// The project's other addresses send every path on to the site's own domain (Kaniel 2026-10-20),
// so search engines and link previews only ever see bloodborne.seamlesscoop.dev. The launcher's
// news URL on the old vercel.app address keeps working through the redirect.
const otherHosts = [
  "www.seamlesscoop.dev",
  "seamlesscoop.dev",
  "bloodborne-seamless-coop.vercel.app",
];

const nextConfig: NextConfig = {
  poweredByHeader: false,
  async redirects() {
    return otherHosts.map((host) => ({
      source: "/:path*",
      has: [{ type: "host" as const, value: host }],
      destination: `https://${SITE_DOMAIN}/:path*`,
      permanent: true,
    }));
  },
  cacheComponents: true,
  partialPrefetching: true,
  turbopack: {
    rules: {
      "*.css": {
        loaders: ["@tailwindcss/turbopack"],
        as: "*.css",
      },
    },
  },
};

export default nextConfig;
