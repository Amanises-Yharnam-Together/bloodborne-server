import Link from "next/link";

import { LogoMark } from "@/components/logo";
import { MenuRule } from "@/components/ornament";
import { ISSUES_URL, REPO_URL, site } from "@/lib/site";

export function SiteFooter() {
  return (
    <footer className="mt-28 pb-10">
      <div className="mx-auto max-w-6xl px-4 sm:px-6">
        <MenuRule crest={false} />
        <div className="mt-8 flex flex-col gap-6 sm:flex-row sm:items-start sm:justify-between">
          <p className="flex items-center gap-2.5 text-bone">
            <LogoMark className="size-6" />
            {site.name}
          </p>
          <nav
            aria-label="Footer"
            className="flex flex-wrap gap-x-7 gap-y-2 text-base sm:justify-end"
          >
            <Link href="/download" className="nav-link">
              Download
            </Link>
            <Link href="/docs/install" className="nav-link">
              Install guide
            </Link>
            <Link href="/docs/launcher" className="nav-link">
              Launcher guide
            </Link>
            <a href={REPO_URL} rel="noopener" className="nav-link">
              Source code
            </a>
            <a href={ISSUES_URL} rel="noopener" className="nav-link">
              Report a bug
            </a>
            <Link href="/docs/credits" className="nav-link">
              Credits
            </Link>
          </nav>
        </div>
        <p className="mt-6 max-w-3xl text-sm leading-relaxed text-muted-foreground/80">
          A free fan project under the GPL. Bloodborne is a trademark of Sony Interactive
          Entertainment; this project contains no game code or assets and is not affiliated with or
          endorsed by Sony Interactive Entertainment or FromSoftware.
        </p>
      </div>
    </footer>
  );
}
