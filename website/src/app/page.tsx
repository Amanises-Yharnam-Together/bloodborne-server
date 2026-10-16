import Link from "next/link";
import { BookOpen, Server, Skull, Swords, Users, Wifi, Moon, Flame } from "lucide-react";

import { ArchitectureDiagram } from "@/components/architecture-diagram";
import { DownloadButton } from "@/components/download-button";
import { SectionHeading } from "@/components/ornament";
import { VideoSlot } from "@/components/video-slot";
import {
  Accordion,
  AccordionContent,
  AccordionItem,
  AccordionTrigger,
} from "@/components/ui/accordion";
import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from "@/components/ui/card";
import { faq } from "@/content/faq";
import { JsonLd } from "@/lib/json-ld";
import { RELEASES_URL, REPO_URL, netcodeStatus, site, siteUrl } from "@/lib/site";

const pillars = [
  {
    icon: Moon,
    title: "A native port",
    text: "Bloodborne PC runs the PS4 game's own code on your CPU and draws it with Vulkan. No emulator in between: 60 FPS and beyond, up to 4K, DLSS, FSR, XeSS, frame generation and HDR.",
  },
  {
    icon: Users,
    title: "Seamless co-op",
    text: "Make a team in the game's own menus and load in together, wherever each of you stands. No bells, no signs, no Insight. Lamps, deaths, bosses and map changes never split the team.",
  },
  {
    icon: Server,
    title: "A new netcode",
    text: "The game's networking is rewritten around a server: every game keeps one UDP link to it, and the server owns the team's world. No peer-to-peer, with protobuf messages and our own reliability layer.",
  },
];

const features = [
  {
    icon: Users,
    title: "Up to four hunters",
    text: "Every remote slot the game has, in one team and one world.",
  },
  {
    icon: Swords,
    title: "One shared world",
    text: "Enemies, their HP and their deaths are shared: every hit lands on the same health bar.",
  },
  {
    icon: Skull,
    title: "Party death",
    text: "A killing blow downs you while a partner stands near; they strike you to revive you.",
  },
  {
    icon: Flame,
    title: "Team worlds on the server",
    text: "Lamps, doors, shortcuts and bosses are saved per team on disk. A crash loses nothing.",
  },
  {
    icon: Wifi,
    title: "Self-hosted",
    text: "One player hosts on their own PC. No PlayStation Network, no cloud, nothing to pay.",
  },
  {
    icon: BookOpen,
    title: "Open source",
    text: "GPL v2. Every hook in the game is listed and checked, and changes run on a multi-game test rig.",
  },
];

export default function Home() {
  return (
    <>
      <section className="relative overflow-hidden">
        <div
          aria-hidden
          className="pointer-events-none absolute -top-40 left-1/2 size-[42rem] -translate-x-1/2 rounded-full bg-[radial-gradient(circle,rgb(232_220_196/0.16),rgb(232_220_196/0.04)_45%,transparent_70%)]"
        />
        <div className="relative mx-auto max-w-4xl px-4 pt-20 pb-14 text-center sm:px-6 sm:pt-28">
          <div className="flex flex-wrap justify-center gap-2">
            <Badge>Free and open source</Badge>
            <Badge variant="muted">Windows</Badge>
            <Badge variant="muted">Bloodborne 1.09</Badge>
          </div>
          <h1 className="mt-8 font-display text-4xl leading-[1.1] tracking-[0.04em] text-bone sm:text-6xl">
            Bloodborne
            <span className="mt-2 block text-2xl tracking-[0.3em] text-gilt sm:text-3xl">
              Seamless Co-op
            </span>
          </h1>
          <p className="mx-auto mt-8 max-w-2xl text-xl leading-relaxed text-foreground sm:text-2xl">
            Hunt Yharnam together on PC. A native port of the game, tuned for co-op, with its
            networking rewritten around a server:{" "}
            <em className="text-bone">no bells, no signs, no peer-to-peer.</em>
          </p>
          <div className="mt-10 flex flex-col items-center justify-center gap-4 sm:flex-row">
            <DownloadButton />
            <Button asChild variant="outline" size="lg">
              <Link href="/docs">Read the docs</Link>
            </Button>
          </div>
          <p className="mt-6 text-base text-muted-foreground">
            Requires your own copy of Bloodborne for PS4 (1.09). No game files are included.
          </p>
        </div>
      </section>

      <section aria-label="Trailer" className="px-4 sm:px-6">
        <VideoSlot />
      </section>

      <section aria-labelledby="what" className="mx-auto mt-28 max-w-6xl px-4 sm:px-6">
        <SectionHeading eyebrow="What it is" title="One port, made for hunting together" id="what">
          <p>
            {site.name} combines three things: a native PC port of Bloodborne, a co-op layer
            inspired by Elden Ring&apos;s Seamless Co-op, and a full rewrite of the game&apos;s
            networking so that a server, not a web of peers, holds the hunt together.
          </p>
        </SectionHeading>
        <div className="mt-14 grid gap-6 md:grid-cols-3">
          {pillars.map(({ icon: Icon, title, text }) => (
            <Card key={title}>
              <CardHeader>
                <Icon className="size-6 text-gilt" aria-hidden />
                <CardTitle className="mt-2">{title}</CardTitle>
              </CardHeader>
              <CardContent>
                <CardDescription>{text}</CardDescription>
              </CardContent>
            </Card>
          ))}
        </div>
      </section>

      <section aria-labelledby="netcode" className="mx-auto mt-28 max-w-6xl px-4 sm:px-6">
        <SectionHeading eyebrow="The netcode" title="Every hunter, one server" id="netcode">
          <p>
            Bloodborne&apos;s own netcode assumes a short visit to one host&apos;s world. Seamless
            co-op breaks that, so the networking was rebuilt: the server orders and saves every
            kill, flag, door and lamp, names one game per map to run its enemies, and relays the
            game&apos;s own packets to the members who need them.
          </p>
        </SectionHeading>
        <div className="mt-14">
          <ArchitectureDiagram />
        </div>
        <div className="mt-10 flex flex-col items-center gap-4 text-center">
          <Badge variant="blood">{netcodeStatus.label}</Badge>
          <p className="max-w-2xl text-base text-muted-foreground">{netcodeStatus.note}</p>
          <Button asChild variant="outline">
            <Link href="/docs/netcode">How the netcode works</Link>
          </Button>
        </div>
      </section>

      <section aria-labelledby="features" className="mx-auto mt-28 max-w-6xl px-4 sm:px-6">
        <SectionHeading eyebrow="In the hunt" title="What you get" id="features" />
        <ul className="mt-14 grid gap-x-10 gap-y-10 sm:grid-cols-2 lg:grid-cols-3">
          {features.map(({ icon: Icon, title, text }) => (
            <li key={title} className="flex gap-4">
              <span className="flex size-10 shrink-0 rotate-45 items-center justify-center border border-gilt/40">
                <Icon className="size-4 -rotate-45 text-gilt" aria-hidden />
              </span>
              <div>
                <h3 className="font-display text-base tracking-[0.08em] text-bone">{title}</h3>
                <p className="mt-1 text-base text-muted-foreground">{text}</p>
              </div>
            </li>
          ))}
        </ul>
      </section>

      <section aria-labelledby="faq" className="mx-auto mt-28 max-w-3xl px-4 sm:px-6">
        <SectionHeading eyebrow="Questions" title="Before you set out" id="faq" />
        <Accordion type="single" collapsible className="frame mt-12 bg-card px-6">
          {faq.map(({ q, a }) => (
            <AccordionItem key={q} value={q}>
              <AccordionTrigger>{q}</AccordionTrigger>
              <AccordionContent>{a}</AccordionContent>
            </AccordionItem>
          ))}
        </Accordion>
      </section>

      <section aria-labelledby="start" className="mx-auto mt-28 max-w-3xl px-4 text-center sm:px-6">
        <SectionHeading eyebrow="Begin the hunt" title="Grab the latest release" id="start">
          <p>
            One installer for every player. The launcher finds your game, sets up co-op and, on the
            host, starts the server.
          </p>
        </SectionHeading>
        <div className="mt-10 flex flex-col items-center justify-center gap-4 sm:flex-row">
          <DownloadButton />
          <Button asChild variant="outline" size="lg">
            <Link href="/docs/install">Install guide</Link>
          </Button>
        </div>
        <p className="mt-6 text-base text-muted-foreground">
          All versions are on{" "}
          <a
            href={RELEASES_URL}
            rel="noopener"
            className="text-gilt underline-offset-4 hover:underline"
          >
            GitHub releases
          </a>
          .
        </p>
      </section>

      <JsonLd
        data={{
          "@context": "https://schema.org",
          "@type": "SoftwareApplication",
          name: site.name,
          description: site.description,
          url: siteUrl,
          applicationCategory: "GameApplication",
          operatingSystem: "Windows 10, Windows 11",
          license: "https://spdx.org/licenses/GPL-2.0-or-later.html",
          isAccessibleForFree: true,
          offers: { "@type": "Offer", price: "0", priceCurrency: "USD" },
          downloadUrl: RELEASES_URL,
          codeRepository: REPO_URL,
          softwareRequirements: "Bloodborne for PS4, version 1.09 (not included)",
        }}
      />
      <JsonLd
        data={{
          "@context": "https://schema.org",
          "@type": "FAQPage",
          mainEntity: faq.map(({ q, a }) => ({
            "@type": "Question",
            name: q,
            acceptedAnswer: { "@type": "Answer", text: a },
          })),
        }}
      />
    </>
  );
}
