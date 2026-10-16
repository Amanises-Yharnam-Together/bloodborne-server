import Link from "next/link";

import { MenuTitle } from "@/components/ornament";

export default function NotFound() {
  return (
    <div className="mx-auto max-w-2xl px-4 pt-28 text-center sm:px-6">
      <MenuTitle as="h1">Nothing here</MenuTitle>
      <p className="mt-6 text-muted-foreground">This page does not exist.</p>
      <nav className="mt-8 flex flex-col items-center" aria-label="Pages">
        <Link href="/" className="menu-item">
          Title screen
        </Link>
        <Link href="/docs/install" className="menu-item">
          Install guide
        </Link>
      </nav>
    </div>
  );
}
