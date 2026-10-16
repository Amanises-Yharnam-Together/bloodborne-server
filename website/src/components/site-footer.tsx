import Link from "next/link";

import { MenuRule, PadGlyph } from "@/components/ornament";
import { ISSUES_URL, REPO_URL } from "@/lib/site";

// The footer reads like the game's key hints along the bottom of a menu.
export function SiteFooter() {
  return (
    <footer className="mt-28 pb-10">
      <div className="mx-auto max-w-5xl px-4 sm:px-6">
        <MenuRule />
        <nav
          aria-label="Footer"
          className="mt-5 flex flex-wrap justify-center gap-x-8 gap-y-2 text-base text-muted-foreground"
        >
          <Link href="/download" className="hover:text-bone">
            <PadGlyph button="cross" /> Download
          </Link>
          <Link href="/docs/install" className="hover:text-bone">
            <PadGlyph button="circle" /> Install guide
          </Link>
          <a href={REPO_URL} rel="noopener" className="hover:text-bone">
            <PadGlyph button="triangle" /> Source code
          </a>
          <a href={ISSUES_URL} rel="noopener" className="hover:text-bone">
            <PadGlyph button="square" /> Report a bug
          </a>
          <Link href="/docs/credits" className="hover:text-bone">
            Credits
          </Link>
        </nav>
        <p className="mx-auto mt-6 max-w-3xl text-center text-sm leading-relaxed text-muted-foreground/80">
          A free fan project under the GPL. Bloodborne is a trademark of Sony Interactive
          Entertainment; this project contains no game code or assets and is not affiliated with or
          endorsed by Sony Interactive Entertainment or FromSoftware.
        </p>
      </div>
    </footer>
  );
}
