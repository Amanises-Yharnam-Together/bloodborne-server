import type { MetadataRoute } from "next";

import { docs } from "@/content/docs";
import { absoluteUrl } from "@/lib/site";

export default function sitemap(): MetadataRoute.Sitemap {
  return [
    { url: absoluteUrl("/"), changeFrequency: "weekly", priority: 1 },
    { url: absoluteUrl("/download"), changeFrequency: "weekly", priority: 0.9 },
    { url: absoluteUrl("/docs"), changeFrequency: "monthly", priority: 0.8 },
    ...docs.map((d) => ({
      url: absoluteUrl(`/docs/${d.slug}`),
      changeFrequency: "monthly" as const,
      priority: 0.7,
    })),
  ];
}
