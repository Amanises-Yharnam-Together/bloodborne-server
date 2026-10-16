import type { Metadata } from "next";
import Link from "next/link";

import { DocsShell } from "@/components/doc-article";
import { Ornament } from "@/components/ornament";
import { Card, CardDescription, CardHeader, CardTitle } from "@/components/ui/card";
import { docs } from "@/content/docs";
import { REPO_URL } from "@/lib/site";

export const metadata: Metadata = {
  title: "Documentation",
  description:
    "How Bloodborne Seamless Co-op is built: install and play, the native port and co-op code, the server netcode, building from source, and credits.",
  alternates: { canonical: "/docs" },
};

export default function DocsIndex() {
  return (
    <DocsShell>
      <div className="max-w-3xl">
        <p className="eyebrow">Docs</p>
        <h1 className="mt-3 font-display text-3xl tracking-[0.04em] text-bone sm:text-4xl">
          Documentation
        </h1>
        <p className="mt-4 text-xl text-muted-foreground">
          How to play, and how it is built: a native port of the game, the co-op code compiled into
          it, and a server that owns the team&apos;s world. The design notes in the{" "}
          <a href={REPO_URL} rel="noopener" className="text-gilt hover:underline">
            repository
          </a>{" "}
          go deeper still.
        </p>
        <Ornament className="mt-8" />
        <ul className="mt-10 grid gap-5 sm:grid-cols-2">
          {docs.map((d) => (
            <li key={d.slug}>
              <Link href={`/docs/${d.slug}`} className="group block h-full">
                <Card className="h-full transition-colors group-hover:bg-accent">
                  <CardHeader>
                    <CardTitle>{d.title}</CardTitle>
                    <CardDescription>{d.description}</CardDescription>
                  </CardHeader>
                </Card>
              </Link>
            </li>
          ))}
        </ul>
      </div>
    </DocsShell>
  );
}
