import Link from "next/link";

import { LogoMark } from "@/components/logo";
import { REPO_URL, site } from "@/lib/site";

const nav = [
  { href: "/download", label: "Download" },
  { href: "/docs", label: "Docs" },
  { href: "/docs/netcode", label: "Netcode" },
];

export function SiteHeader() {
  return (
    <header className="sticky top-0 z-40 border-b border-border/80 bg-background/85 backdrop-blur-md">
      <div className="mx-auto flex max-w-6xl flex-wrap items-center justify-between gap-x-6 gap-y-2 px-4 py-3 sm:px-6">
        <Link href="/" className="group flex items-center gap-3" aria-label={`${site.name}, home`}>
          <LogoMark className="size-8 transition-[filter] duration-300 group-hover:drop-shadow-[0_0_8px_rgb(232_220_196/0.5)]" />
          <span className="font-display text-sm uppercase tracking-[0.2em] text-bone">
            {site.shortName}
          </span>
        </Link>
        <nav aria-label="Main" className="flex items-center gap-1 sm:gap-2">
          {nav.map((item) => (
            <Link
              key={item.href}
              href={item.href}
              className="px-2.5 py-1.5 font-display text-xs uppercase tracking-[0.18em] text-muted-foreground transition-colors hover:text-bone sm:px-3"
            >
              {item.label}
            </Link>
          ))}
          <a
            href={REPO_URL}
            rel="noopener"
            className="px-2.5 py-1.5 font-display text-xs uppercase tracking-[0.18em] text-muted-foreground transition-colors hover:text-bone sm:px-3"
          >
            GitHub
          </a>
        </nav>
      </div>
    </header>
  );
}
