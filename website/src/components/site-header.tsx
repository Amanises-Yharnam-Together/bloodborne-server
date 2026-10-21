import Link from "next/link";

import { LogoMark } from "@/components/logo";
import { SocialLinks } from "@/components/social-links";
import { REPO_URL, site } from "@/lib/site";

const nav = [
  { href: "/download", label: "Download" },
  { href: "/docs/install", label: "Install" },
  { href: "/docs/play", label: "Play together" },
  { href: "/docs/architecture", label: "How it works" },
];

export function SiteHeader() {
  return (
    <header className="relative z-40 border-b border-silver/15 bg-black/60">
      <div className="mx-auto flex max-w-6xl flex-wrap items-center justify-between gap-x-6 gap-y-1 px-4 py-3 sm:px-6">
        <Link
          href="/"
          className="flex items-center gap-2.5 py-1 text-lg text-bone hover:text-title"
          aria-label={`${site.name}, home`}
        >
          <LogoMark className="size-9" />
          {site.shortName}
        </Link>
        <nav
          aria-label="Main"
          className="flex flex-wrap items-center gap-x-5 gap-y-1 text-base sm:gap-x-7"
        >
          {nav.map((item) => (
            <Link key={item.href} href={item.href} className="nav-link py-1">
              {item.label}
            </Link>
          ))}
          <a href={REPO_URL} rel="noopener" className="nav-link hidden py-1 sm:inline">
            GitHub
          </a>
          <SocialLinks className="hidden sm:flex" />
        </nav>
      </div>
    </header>
  );
}
