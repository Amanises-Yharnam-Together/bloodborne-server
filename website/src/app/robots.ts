import type { MetadataRoute } from "next";

import { absoluteUrl, siteUrl } from "@/lib/site";

// Open to every crawler, AI ones included: the point of the site is to be found.
export default function robots(): MetadataRoute.Robots {
  return {
    rules: [{ userAgent: "*", allow: "/" }],
    sitemap: absoluteUrl("/sitemap.xml"),
    host: siteUrl,
  };
}
