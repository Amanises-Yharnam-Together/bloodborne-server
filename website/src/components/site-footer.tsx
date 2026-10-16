import Link from "next/link";

import { LogoMark } from "@/components/logo";
import { Ornament } from "@/components/ornament";
import { ISSUES_URL, RELEASES_URL, REPO_URL, site } from "@/lib/site";

export function SiteFooter() {
  return (
    <footer className="mt-24 border-t border-border">
      <div className="mx-auto max-w-6xl px-4 py-12 sm:px-6">
        <div className="flex flex-col gap-10 sm:flex-row sm:justify-between">
          <div className="max-w-sm">
            <div className="flex items-center gap-3">
              <LogoMark className="size-7" />
              <span className="font-display text-sm uppercase tracking-[0.2em] text-bone">
                {site.name}
              </span>
            </div>
            <p className="mt-4 text-base text-muted-foreground">
              A free fan project, open source under the GPL. Bring your own copy of the game.
            </p>
          </div>
          <nav aria-label="Footer" className="grid grid-cols-2 gap-x-12 gap-y-2 text-base">
            <Link href="/download" className="text-muted-foreground hover:text-bone">
              Download
            </Link>
            <a href={RELEASES_URL} rel="noopener" className="text-muted-foreground hover:text-bone">
              Releases
            </a>
            <Link href="/docs" className="text-muted-foreground hover:text-bone">
              Documentation
            </Link>
            <a href={REPO_URL} rel="noopener" className="text-muted-foreground hover:text-bone">
              Source code
            </a>
            <Link href="/docs/credits" className="text-muted-foreground hover:text-bone">
              Credits
            </Link>
            <a href={ISSUES_URL} rel="noopener" className="text-muted-foreground hover:text-bone">
              Report a bug
            </a>
          </nav>
        </div>
        <Ornament className="my-10" />
        <p className="text-sm leading-relaxed text-muted-foreground">
          Bloodborne is a trademark of Sony Interactive Entertainment. This is an unofficial fan
          project: it contains no game code or assets and is not affiliated with, endorsed or
          sponsored by Sony Interactive Entertainment or FromSoftware.
        </p>
      </div>
    </footer>
  );
}
