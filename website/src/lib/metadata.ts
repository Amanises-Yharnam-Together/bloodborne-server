import type { Metadata } from "next";

import { site } from "@/lib/site";

export const previewImage = {
  url: "/opengraph-image",
  width: 1200,
  height: 630,
  alt: `${site.shortName}: Bloodborne seamless co-op on PC`,
};

/**
 * A page's metadata with its own link-card tags. Next replaces a parent's openGraph and twitter
 * objects whole rather than merging them, and a page that sets its own openGraph loses the root
 * opengraph-image too, so every page sets the site name, locale, card type and the social preview
 * (src/app/opengraph-image.tsx) again here.
 */
export function pageMetadata({
  title,
  description,
  path,
  type = "website",
}: {
  title: string;
  description: string;
  path: string;
  type?: "website" | "article";
}): Metadata {
  const cardTitle = `${title} | ${site.shortName}`;
  return {
    title,
    description,
    alternates: { canonical: path },
    openGraph: {
      type,
      siteName: site.name,
      locale: "en",
      title: cardTitle,
      description,
      url: path,
      images: [previewImage],
    },
    twitter: { card: "summary_large_image", title: cardTitle, description, images: [previewImage] },
  };
}
