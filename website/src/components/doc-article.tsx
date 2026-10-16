import type { Metadata } from "next";
import Link from "next/link";
import { notFound } from "next/navigation";

import { DocBody } from "@/components/doc-blocks";
import { MenuTitle } from "@/components/ornament";
import { docs, getDoc } from "@/content/docs";
import { JsonLd } from "@/lib/json-ld";
import { absoluteUrl, site } from "@/lib/site";

export function docMetadata(slug: string): Metadata {
  const page = getDoc(slug);
  if (!page) return {};
  return {
    title: page.title,
    description: page.description,
    alternates: { canonical: `/docs/${slug}` },
    openGraph: {
      type: "article",
      title: page.title,
      description: page.description,
      url: `/docs/${slug}`,
    },
  };
}

/** The docs' pages as a row of tabs, like the menus along the top of the game's inventory. */
export function DocsNav({ current }: { current?: string }) {
  const items: { href: string; slug?: string; title: string }[] = [
    { href: "/docs", title: "Guide" },
    ...docs.map((d) => ({ href: `/docs/${d.slug}`, slug: d.slug, title: d.title })),
  ];
  return (
    <nav aria-label="Documentation" className="border-b border-silver/15">
      <ul className="-mb-px flex flex-wrap justify-center text-base">
        {items.map((d) => (
          <li key={d.href}>
            <Link
              href={d.href}
              aria-current={current === d.slug ? "page" : undefined}
              className="menu-item px-4!"
            >
              {d.title}
            </Link>
          </li>
        ))}
      </ul>
    </nav>
  );
}

export function DocsShell({ current, children }: { current?: string; children: React.ReactNode }) {
  return (
    <div className="mx-auto max-w-6xl px-4 pt-6 sm:px-6">
      <DocsNav current={current} />
      <div className="min-w-0 pt-12">{children}</div>
    </div>
  );
}

export function DocArticle({ slug }: { slug: string }) {
  const page = getDoc(slug);
  if (!page) notFound();
  const i = docs.indexOf(page);
  const prev = docs[i - 1];
  const next = docs[i + 1];
  return (
    <DocsShell current={slug}>
      <article>
        <header className="text-center">
          <MenuTitle as="h1">{page.title}</MenuTitle>
          <p className="mx-auto mt-5 max-w-2xl text-lg text-muted-foreground italic">
            {page.description}
          </p>
        </header>
        <div className="mt-10">
          <DocBody page={page} />
        </div>
        <nav
          aria-label="Next and previous"
          className="mt-12 flex flex-wrap justify-between gap-4 border-t border-silver/15 pt-6"
        >
          {prev ? (
            <Link href={`/docs/${prev.slug}`} className="menu-item">
              <span className="text-muted-foreground">Back: </span>
              {prev.title}
            </Link>
          ) : (
            <span />
          )}
          {next ? (
            <Link href={`/docs/${next.slug}`} className="menu-item">
              <span className="text-muted-foreground">Next: </span>
              {next.title}
            </Link>
          ) : null}
        </nav>
      </article>
      <JsonLd
        data={{
          "@context": "https://schema.org",
          "@type": "TechArticle",
          headline: page.title,
          description: page.description,
          url: absoluteUrl(`/docs/${slug}`),
          isPartOf: {
            "@type": "WebSite",
            name: site.name,
            url: absoluteUrl("/"),
          },
          inLanguage: "en",
        }}
      />
    </DocsShell>
  );
}
