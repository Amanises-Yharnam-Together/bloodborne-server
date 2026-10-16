import type { Metadata } from "next";
import Link from "next/link";
import { notFound } from "next/navigation";

import { DocBody } from "@/components/doc-blocks";
import { Ornament } from "@/components/ornament";
import { docs, getDoc } from "@/content/docs";
import { JsonLd } from "@/lib/json-ld";
import { absoluteUrl, site } from "@/lib/site";
import { cn } from "@/lib/utils";

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

export function DocsNav({ current }: { current?: string }) {
  return (
    <nav aria-label="Documentation" className="text-base">
      <p className="eyebrow mb-3">Documentation</p>
      <ul className="space-y-1 border-l border-border">
        <li>
          <Link
            href="/docs"
            aria-current={current === undefined ? "page" : undefined}
            className={cn(
              "-ml-px block border-l px-4 py-1 transition-colors",
              current === undefined
                ? "border-gilt text-bone"
                : "border-transparent text-muted-foreground hover:text-bone",
            )}
          >
            Overview
          </Link>
        </li>
        {docs.map((d) => (
          <li key={d.slug}>
            <Link
              href={`/docs/${d.slug}`}
              aria-current={current === d.slug ? "page" : undefined}
              className={cn(
                "-ml-px block border-l px-4 py-1 transition-colors",
                current === d.slug
                  ? "border-gilt text-bone"
                  : "border-transparent text-muted-foreground hover:text-bone",
              )}
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
    <div className="mx-auto grid max-w-6xl gap-10 px-4 pt-12 sm:px-6 lg:grid-cols-[14rem_minmax(0,1fr)] lg:pt-16">
      <aside className="lg:sticky lg:top-24 lg:self-start">
        <DocsNav current={current} />
      </aside>
      <div className="min-w-0">{children}</div>
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
      <article className="max-w-3xl">
        <header>
          <p className="eyebrow">Docs</p>
          <h1 className="mt-3 font-display text-3xl tracking-[0.04em] text-bone sm:text-4xl">
            {page.title}
          </h1>
          <p className="mt-4 text-xl text-muted-foreground">{page.description}</p>
          <nav aria-label="On this page" className="mt-6 flex flex-wrap gap-x-5 gap-y-1 text-base">
            {page.sections.map((s) => (
              <a key={s.id} href={`#${s.id}`} className="text-gilt/90 hover:text-bone">
                {s.title}
              </a>
            ))}
          </nav>
          <Ornament className="mt-8" />
        </header>
        <DocBody page={page} />
        <nav aria-label="Next and previous" className="mt-16 grid gap-4 sm:grid-cols-2">
          {prev ? (
            <Link
              href={`/docs/${prev.slug}`}
              className="frame block bg-card px-5 py-4 hover:bg-accent"
            >
              <span className="eyebrow">Previous</span>
              <span className="mt-1 block font-display text-bone">{prev.title}</span>
            </Link>
          ) : (
            <span />
          )}
          {next ? (
            <Link
              href={`/docs/${next.slug}`}
              className="frame block bg-card px-5 py-4 text-right hover:bg-accent"
            >
              <span className="eyebrow">Next</span>
              <span className="mt-1 block font-display text-bone">{next.title}</span>
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
