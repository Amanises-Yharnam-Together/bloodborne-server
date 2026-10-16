import Link from "next/link";

import { LogoMark } from "@/components/logo";
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
      <div className="mx-auto flex max-w-6xl flex-wrap items-center justify-between gap-x-6 gap-y-1 px-4 py-2 sm:px-6">
        <Link
          href="/"
          className="flex items-center gap-2.5 py-1 text-lg text-bone hover:text-title"
          aria-label={`${site.name}, home`}
        >
          <LogoMark className="size-7" />
          {site.shortName}
        </Link>
        <nav aria-label="Main" className="-mx-2 flex flex-wrap items-center text-base sm:-mx-3">
          {nav.map((item) => (
            <Link key={item.href} href={item.href} className="menu-item px-2! sm:px-3!">
              {item.label}
            </Link>
          ))}
          <a href={REPO_URL} rel="noopener" className="menu-item hidden! px-3! sm:block!">
            GitHub
          </a>
        </nav>
      </div>
    </header>
  );
}
