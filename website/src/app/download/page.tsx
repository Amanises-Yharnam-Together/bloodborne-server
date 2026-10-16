import type { Metadata } from "next";
import Link from "next/link";
import { Download, FileCheck2 } from "lucide-react";

import { MenuTitle } from "@/components/ornament";
import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import { Card, CardContent, CardHeader, CardTitle } from "@/components/ui/card";
import { formatBytes, getLatestRelease, type Release } from "@/lib/github";
import { RELEASES_URL } from "@/lib/site";

export const metadata: Metadata = {
  title: "Download",
  description:
    "Download the latest release of Bloodborne Seamless Co-op for Windows: one installer for every player, free and open source. Requires your own Bloodborne 1.09.",
  alternates: { canonical: "/download" },
};

export default function DownloadPage() {
  return (
    <div className="mx-auto max-w-4xl px-4 pt-16 sm:px-6">
      <MenuTitle as="h1">Download</MenuTitle>
      <p className="mx-auto mt-5 max-w-xl text-center text-lg text-muted-foreground italic">
        For Windows 10 and 11. Everyone in a team installs the same release.
      </p>

      <div className="mt-12">
        <LatestRelease />
      </div>

      <dl className="mx-auto mt-14 max-w-3xl">
        {[
          [
            "You need",
            "Your own Bloodborne for PS4 at 1.09 (a game folder with the update, or the .pkg files), and a Vulkan 1.3 graphics card.",
          ],
          ["Then", "Run the setup, open the launcher, fill in the Co-op card and press PLAY."],
          [
            "Players",
            "A way to reach the host's PC: the same VPN, or ports forwarded on the host's router.",
          ],
        ].map(([term, text]) => (
          <div
            key={term}
            className="grid gap-x-8 gap-y-1 border-b border-silver/15 py-4 sm:grid-cols-[minmax(0,10rem)_minmax(0,1fr)]"
          >
            <dt className="text-xl text-bone sm:text-right">{term}</dt>
            <dd>{text}</dd>
          </div>
        ))}
      </dl>
      <p className="mt-8 text-center">
        <Link href="/docs/install" className="menu-item inline-block">
          The install guide, step by step
        </Link>
      </p>
    </div>
  );
}

async function LatestRelease() {
  const release = await getLatestRelease();
  if (!release) return <Fallback />;
  return <ReleaseCard release={release} />;
}

function ReleaseCard({ release }: { release: Release }) {
  const date = release.publishedAt
    ? new Date(release.publishedAt).toLocaleDateString("en", {
        year: "numeric",
        month: "long",
        day: "numeric",
        timeZone: "UTC",
      })
    : null;
  return (
    <Card className="gap-6">
      <CardHeader className="sm:grid-cols-[1fr_auto] sm:items-start">
        <div>
          <Badge>Latest release</Badge>
          <CardTitle className="mt-3 text-3xl">{release.name}</CardTitle>
          {date ? (
            <p className="mt-1 text-base text-muted-foreground">
              Published <time dateTime={release.publishedAt}>{date}</time>
            </p>
          ) : null}
        </div>
        <Button asChild variant="outline" size="sm">
          <a href={release.url} rel="noopener">
            Release notes
          </a>
        </Button>
      </CardHeader>
      <CardContent>
        <ul className="divide-y divide-border border-y border-border">
          {release.assets.map((a) => (
            <li
              key={a.name}
              className="flex flex-col gap-3 py-4 sm:flex-row sm:items-center sm:justify-between"
            >
              <div className="min-w-0">
                <p className="truncate font-mono text-sm text-bone">{a.name}</p>
                <p className="text-sm text-muted-foreground">
                  {formatBytes(a.size)}
                  {a.checksumUrl ? (
                    <>
                      {" · "}
                      <a
                        href={a.checksumUrl}
                        rel="noopener"
                        className="inline-flex items-center gap-1 text-title hover:underline"
                      >
                        <FileCheck2 className="size-3.5" aria-hidden />
                        SHA-256
                      </a>
                    </>
                  ) : null}
                </p>
              </div>
              <Button asChild>
                <a href={a.url} rel="noopener">
                  <Download aria-hidden />
                  Download
                </a>
              </Button>
            </li>
          ))}
        </ul>
        <p className="mt-4 text-sm text-muted-foreground">
          Older versions are on{" "}
          <a href={RELEASES_URL} rel="noopener" className="text-title hover:underline">
            GitHub releases
          </a>
          .
        </p>
      </CardContent>
    </Card>
  );
}

function Fallback() {
  return (
    <Card className="items-center text-center">
      <CardContent className="flex flex-col items-center gap-4">
        <p className="text-base text-muted-foreground">
          Every release, with its installer and SHA-256, is on GitHub.
        </p>
        <Button asChild size="lg">
          <a href={RELEASES_URL} rel="noopener">
            <Download aria-hidden />
            Open GitHub releases
          </a>
        </Button>
      </CardContent>
    </Card>
  );
}
