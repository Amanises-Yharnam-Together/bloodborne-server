import type { Metadata } from "next";
import Link from "next/link";
import { notFound } from "next/navigation";

import { DocBody } from "@/components/doc-blocks";
import { Heading } from "@/components/ornament";
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

/** The guide's pages: a column beside the text on wide screens, a wrapped row above it on
 *  narrow ones. */
export function DocsNav({ current }: { current?: string }) {
  const items: { href: string; slug?: string; title: string }[] = [
    { href: "/docs", title: "Overview" },
    ...docs.map((d) => ({ href: `/docs/${d.slug}`, slug: d.slug, title: d.title })),
  ];
  return (
    <nav aria-label="Guide" className="lg:sticky lg:top-8">
      <p className="text-lg text-title">The guide</p>
      <ul className="mt-3 flex flex-wrap gap-x-5 gap-y-1.5 text-base lg:flex-col lg:border-l lg:border-silver/20">
        {items.map((d) => {
          const here = current === d.slug;
          return (
            <li key={d.href}>
              <Link
                href={d.href}
                aria-current={here ? "page" : undefined}
                className={
                  "nav-link block lg:-ml-px lg:border-l lg:py-0.5 lg:pl-4 " +
                  (here ? "lg:border-title" : "lg:border-transparent")
                }
              >
                {d.title}
              </Link>
            </li>
          );
        })}
      </ul>
    </nav>
  );
}

export function DocsShell({ current, children }: { current?: string; children: React.ReactNode }) {
  return (
    <div className="mx-auto grid max-w-6xl gap-x-14 gap-y-10 px-4 pt-12 sm:px-6 lg:grid-cols-[12rem_minmax(0,1fr)]">
      <aside className="border-b border-silver/15 pb-6 lg:border-0 lg:pb-0">
        <DocsNav current={current} />
      </aside>
      <div className="min-w-0 max-w-[52rem]">{children}</div>
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
        <header>
          <Heading as="h1">{page.title}</Heading>
          <p className="mt-5 max-w-2xl text-lg text-muted-foreground">{page.description}</p>
        </header>
        <div className="mt-6">
          <DocBody page={page} />
        </div>
        <nav
          aria-label="Next and previous"
          className="mt-12 flex flex-wrap justify-between gap-4 border-t border-silver/15 pt-6"
        >
          {prev ? (
            <Link href={`/docs/${prev.slug}`} className="text-bone hover:text-title">
              <span className="text-muted-foreground">Back: </span>
              {prev.title}
            </Link>
          ) : (
            <span />
          )}
          {next ? (
            <Link href={`/docs/${next.slug}`} className="text-bone hover:text-title">
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
