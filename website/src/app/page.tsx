import Image from "next/image";
import Link from "next/link";

import { DownloadButton } from "@/components/download-button";
import { Emblem } from "@/components/logo";
import { Heading } from "@/components/ornament";
import { VideoSlot } from "@/components/video-slot";
import { JsonLd } from "@/lib/json-ld";
import { REPO_URL, site, siteUrl } from "@/lib/site";

// The rest of what changes, in a few words each.
const changes: [string, string][] = [
  ["One world", "Enemies, bosses, doors, lamps and shortcuts are shared by the whole team."],
  [
    "Your own things",
    "Pickups, Blood Echoes and Insight stay yours. NPC gifts come to each of you.",
  ],
  ["Free travel", "Everyone goes where they like. Nobody is pulled along or sent home."],
  ["Up to four", "Four hunters in a team, made in the game's own menus."],
  ["Native port", "Bloodborne PC, not an emulator: 60 FPS, 4K, DLSS, FSR and XeSS."],
  ["Your server", "One of you hosts it next to the game. No accounts, no cloud."],
];

const features = [
  {
    title: "Your teammates on screen",
    text: "Each teammate sits on the left under your own bars, with a portrait, their name and their health. When they are on another map the list says so.",
    href: "/docs/play#party",
    src: "/docs/party-alive.webp",
    width: 1040,
    height: 340,
    alt: "A teammate in the party list: a round framed portrait, the name Player 2 and a health bar.",
  },
  {
    title: "Down, not dead",
    text: "A killing blow puts you down while a teammate is on your map. They strike you to raise you; if everyone goes down, the party wakes at its lamps.",
    href: "/docs/play#down",
    src: "/docs/party-downed.webp",
    width: 1040,
    height: 340,
    alt: "The same teammate in the party list while down: a blood drop on the portrait and an empty bar.",
  },
  {
    title: "Hand things over",
    text: "Give a teammate Blood Vials, weapons or Blood Echoes from the inventory. They accept in the game's own dialog.",
    href: "/docs/play#give",
    src: "/docs/give-items.webp",
    width: 1280,
    height: 720,
    alt: "A guest receives four Blood Vials from the host in the game's own item dialog.",
  },
];

const steps = [
  {
    title: "Install",
    text: "Run the setup on every PC and point the launcher at your Bloodborne folder.",
    href: "/docs/install#install",
  },
  {
    title: "Fill in the Co-op card",
    text: "One player is the host and runs the server. The others type the host's address.",
    href: "/docs/install#coop-card",
  },
  {
    title: "Make a team",
    text: "Press PLAY, invite each other on the team screen and begin the hunt.",
    href: "/docs/play#team",
  },
];

export default function Home() {
  return (
    <>
      <section className="mx-auto grid max-w-6xl items-center gap-x-14 gap-y-12 px-4 pt-14 pb-20 sm:px-6 lg:grid-cols-[minmax(0,5fr)_minmax(0,7fr)] lg:pt-20">
        <div>
          <Emblem priority className="size-20 drop-shadow-[0_0_30px_rgb(190_186_176/0.15)]" />
          <h1 className="mt-6 text-5xl leading-[1.05] font-normal text-bone sm:text-6xl">
            Bloodborne
            <span className="block text-title">Seamless Co-op</span>
          </h1>
          <p className="mt-6 max-w-md text-xl">
            Play the whole game with your friends in one shared world, from the first lamp to the
            last boss.
          </p>
          <div className="mt-8 flex flex-wrap items-center gap-x-6 gap-y-4">
            <DownloadButton />
            <Link
              href="/docs/install"
              className="text-lg text-title underline decoration-title/40 underline-offset-4 hover:text-bone"
            >
              Install guide
            </Link>
          </div>
          <p className="mt-6 text-base text-muted-foreground">
            Windows 10 and 11. You need your own Bloodborne 1.09 for PS4.
          </p>
        </div>
        <figure>
          <Image
            src="/docs/team-host.webp"
            alt="The Hunt Together team screen with two hunters and two empty places."
            width={1200}
            height={771}
            priority
            sizes="(min-width: 1024px) 640px, 100vw"
            className="h-auto w-full border-y border-silver/30 shadow-[0_30px_80px_rgb(0_0_0/0.7)]"
          />
          <figcaption className="mt-3 text-base text-muted-foreground italic">
            The team screen, before loading in.
          </figcaption>
        </figure>
      </section>

      <section aria-label="Trailer" className="mx-auto max-w-6xl px-4 sm:px-6">
        <VideoSlot />
      </section>

      <section aria-labelledby="features" className="mx-auto mt-28 max-w-6xl px-4 sm:px-6">
        <Heading id="features">In the game</Heading>
        <div className="mt-12 space-y-20">
          {features.map((f, i) => (
            <article
              key={f.title}
              className="grid items-center gap-x-14 gap-y-6 md:grid-cols-[minmax(0,7fr)_minmax(0,5fr)]"
            >
              <Image
                src={f.src}
                alt={f.alt}
                width={f.width}
                height={f.height}
                sizes="(min-width: 768px) 620px, 100vw"
                className={
                  "h-auto w-full border-y border-silver/30 shadow-[0_20px_60px_rgb(0_0_0/0.6)]" +
                  (i % 2 ? " md:order-2" : "")
                }
              />
              <div>
                <h3 className="text-2xl text-bone">{f.title}</h3>
                <p className="mt-3">{f.text}</p>
                <Link
                  href={f.href}
                  className="mt-4 inline-block text-title underline decoration-title/40 underline-offset-4 hover:text-bone"
                >
                  More in the guide
                </Link>
              </div>
            </article>
          ))}
        </div>

        <dl className="mt-24 grid gap-x-14 gap-y-8 border-t border-silver/15 pt-12 sm:grid-cols-2 lg:grid-cols-3">
          {changes.map(([term, text]) => (
            <div key={term}>
              <dt className="text-xl text-bone">{term}</dt>
              <dd className="mt-1 text-muted-foreground">{text}</dd>
            </div>
          ))}
        </dl>
        <p className="mt-10 max-w-2xl text-muted-foreground">
          Instead of the game&apos;s peer-to-peer links, every game connects to one small server
          that keeps the team&apos;s world.{" "}
          <Link href="/docs/netcode" className="text-title hover:text-bone">
            Why a server
          </Link>
        </p>
      </section>

      <section aria-labelledby="start" className="mx-auto mt-28 max-w-6xl px-4 sm:px-6">
        <Heading id="start">Getting started</Heading>
        <ol className="mt-10 grid gap-x-12 gap-y-10 md:grid-cols-3">
          {steps.map((step, i) => (
            <li key={step.title} className="border-t border-silver/25 pt-5">
              <span className="text-4xl text-title/80">{["I", "II", "III"][i]}</span>
              <h3 className="mt-2 text-2xl text-bone">
                <Link href={step.href} className="hover:text-title">
                  {step.title}
                </Link>
              </h3>
              <p className="mt-2 text-muted-foreground">{step.text}</p>
            </li>
          ))}
        </ol>

        <div className="panel mt-20 flex flex-col gap-6 px-6 py-8 sm:px-10 md:flex-row md:items-center md:justify-between">
          <p className="max-w-xl text-lg">
            One installer for every player. Everyone in a team, the host included, uses the same
            release.
          </p>
          <DownloadButton />
        </div>
      </section>

      <JsonLd
        data={{
          "@context": "https://schema.org",
          "@type": "SoftwareApplication",
          name: site.name,
          description: site.description,
          applicationCategory: "GameApplication",
          operatingSystem: "Windows 10, Windows 11",
          url: siteUrl,
          downloadUrl: `${siteUrl}/download`,
          license: `https://spdx.org/licenses/${site.license}.html`,
          isAccessibleForFree: true,
          offers: { "@type": "Offer", price: "0", priceCurrency: "USD" },
          codeRepository: REPO_URL,
        }}
      />
    </>
  );
}
