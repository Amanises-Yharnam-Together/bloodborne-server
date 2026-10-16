import type { Metadata } from "next";
import Link from "next/link";
import { Download, FileCheck2 } from "lucide-react";

import { SectionHeading } from "@/components/ornament";
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

const steps = [
  "Download the setup and run it. It installs for your Windows user.",
  "Open Bloodborne Seamless Co-op. It finds your game, or installs it from your PS4 .pkg files.",
  "On the Co-op card choose host or player, the host's IP, an account and the party password.",
  "Press PLAY. The host's server starts by itself; allow co-op through the firewall once.",
];

export default function DownloadPage() {
  return (
    <div className="mx-auto max-w-4xl px-4 pt-16 sm:px-6 sm:pt-24">
      <SectionHeading eyebrow="Download" title="Bloodborne Seamless Co-op for Windows">
        <p>Free and open source. Every player in a party installs the same release.</p>
      </SectionHeading>

      <div className="mt-12">
        <LatestRelease />
      </div>

      <div className="mt-12 grid gap-6 md:grid-cols-2">
        <Card>
          <CardHeader>
            <CardTitle>You need</CardTitle>
          </CardHeader>
          <CardContent>
            <ul className="space-y-2 text-base text-muted-foreground">
              <li>
                <strong className="text-bone">Your own Bloodborne for PS4, version 1.09</strong>: a
                dumped game folder with the update, or the .pkg files. No game files are included.
              </li>
              <li>Windows 10 (1903 or later) or 11, 64-bit.</li>
              <li>A Vulkan 1.3 graphics card with a current driver.</li>
              <li>
                For guests: a way to reach the host&apos;s PC, such as a shared VPN (Radmin VPN,
                Tailscale, ZeroTier).
              </li>
            </ul>
          </CardContent>
        </Card>
        <Card>
          <CardHeader>
            <CardTitle>Then</CardTitle>
          </CardHeader>
          <CardContent>
            <ol className="list-decimal space-y-2 pl-5 text-base text-muted-foreground marker:font-display marker:text-gilt">
              {steps.map((s) => (
                <li key={s}>{s}</li>
              ))}
            </ol>
            <Button asChild variant="link" className="mt-4 px-0">
              <Link href="/docs/install">Full install guide</Link>
            </Button>
          </CardContent>
        </Card>
      </div>
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
          <CardTitle className="mt-3 text-2xl">{release.name}</CardTitle>
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
                        className="inline-flex items-center gap-1 text-gilt hover:underline"
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
          <a href={RELEASES_URL} rel="noopener" className="text-gilt hover:underline">
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
