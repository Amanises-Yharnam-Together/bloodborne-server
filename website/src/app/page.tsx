import Image from "next/image";
import Link from "next/link";

import { DownloadButton } from "@/components/download-button";
import { Emblem } from "@/components/logo";
import { MenuRule, MenuTitle } from "@/components/ornament";
import { VideoSlot } from "@/components/video-slot";
import { JsonLd } from "@/lib/json-ld";
import { REPO_URL, site, siteUrl } from "@/lib/site";

// What changes, as the game lists a menu: the name on the left, its value on the right.
const changes: [string, string][] = [
  ["One world", "Enemies, bosses, doors, lamps and shortcuts are shared by the whole team."],
  [
    "Your own things",
    "Pickups, Blood Echoes and Insight stay yours. NPC gifts come to each of you.",
  ],
  ["Free travel", "Everyone goes where they like. Nobody is pulled along or sent home."],
  ["Party death", "A killing blow puts you down. A teammate strikes you to bring you back."],
  ["Up to four", "Four hunters in a team, made in the game's own menus."],
  ["Native port", "Bloodborne PC, not an emulator: 60 FPS, 4K, DLSS, FSR and XeSS."],
  ["Your server", "One of you hosts it next to the game. No accounts, no cloud."],
];

const shots = [
  {
    src: "/docs/team-host.webp",
    width: 1200,
    height: 771,
    alt: "The Hunt Together team screen with two hunters and two empty places.",
    caption: "Making a team before loading in",
  },
  {
    src: "/docs/give-items.webp",
    width: 1280,
    height: 720,
    alt: "A guest receives four Blood Vials from the host in the game's own item dialog.",
    caption: "Blood Vials handed over by a teammate",
  },
];

export default function Home() {
  return (
    <>
      <section className="flex min-h-[calc(100dvh-3.25rem)] flex-col items-center justify-center px-4 py-16 text-center">
        <Emblem
          priority
          className="size-36 drop-shadow-[0_0_40px_rgb(190_186_176/0.18)] sm:size-44"
        />
        <h1 className="mt-8 text-4xl font-normal tracking-[0.01em] text-bone sm:text-6xl">
          Bloodborne
          <span className="mt-1 block text-2xl text-title sm:text-3xl">Seamless Co-op</span>
        </h1>
        <p className="mt-6 max-w-xl text-lg text-muted-foreground">{site.tagline}</p>

        <nav aria-label="Start" className="mt-10 flex w-full max-w-xs flex-col items-stretch">
          <Link href="/download" className="menu-item text-xl">
            Download
          </Link>
          <Link href="/docs/install" className="menu-item text-xl">
            Install guide
          </Link>
          <Link href="/docs/play" className="menu-item text-xl">
            Playing together
          </Link>
          <Link href="/docs/architecture" className="menu-item text-xl">
            How it works
          </Link>
          <a href={REPO_URL} rel="noopener" className="menu-item text-xl">
            Source code
          </a>
        </nav>

        <p className="mt-12 text-sm text-muted-foreground/80">
          For Windows. Bring your own Bloodborne 1.09 for PS4.
        </p>
      </section>

      <section aria-labelledby="trailer" className="mx-auto max-w-5xl px-4 sm:px-6">
        <h2 id="trailer" className="sr-only">
          Trailer
        </h2>
        <VideoSlot />
      </section>

      <section aria-labelledby="what" className="mx-auto mt-24 max-w-4xl px-4 sm:px-6">
        <MenuTitle id="what">What changes</MenuTitle>
        <dl className="mx-auto mt-4 max-w-3xl">
          {changes.map(([term, text]) => (
            <div
              key={term}
              className="grid gap-x-8 gap-y-1 border-b border-silver/15 py-4 sm:grid-cols-[minmax(0,13rem)_minmax(0,1fr)]"
            >
              <dt className="text-xl text-bone sm:text-right">{term}</dt>
              <dd className="text-foreground">{text}</dd>
            </div>
          ))}
        </dl>
        <p className="mx-auto mt-8 max-w-3xl text-center text-muted-foreground">
          Instead of the game&apos;s peer-to-peer links, every game connects to one small server
          that keeps the team&apos;s world.{" "}
          <Link href="/docs/netcode" className="text-title hover:text-bone">
            Why a server
          </Link>
        </p>
      </section>

      <section
        aria-label="Screenshots"
        className="mx-auto mt-24 grid max-w-6xl gap-8 px-4 sm:px-6 md:grid-cols-2"
      >
        {shots.map((s) => (
          <figure key={s.src}>
            <Image
              src={s.src}
              alt={s.alt}
              width={s.width}
              height={s.height}
              sizes="(min-width: 768px) 560px, 100vw"
              className="h-auto w-full border-y border-silver/30 shadow-[0_20px_60px_rgb(0_0_0/0.6)]"
            />
            <figcaption className="mt-3 text-center text-muted-foreground italic">
              {s.caption}
            </figcaption>
          </figure>
        ))}
      </section>

      <section aria-labelledby="get" className="mx-auto mt-24 max-w-3xl px-4 text-center sm:px-6">
        <MenuTitle id="get">Get it</MenuTitle>
        <p className="mx-auto mt-6 max-w-xl">
          One installer for every player. Everyone in a team, the host included, uses the same
          release.
        </p>
        <div className="mt-8 flex flex-wrap items-center justify-center gap-4">
          <DownloadButton />
          <Link href="/docs/install" className="menu-item">
            Read the install guide
          </Link>
        </div>
        <MenuRule crest={false} className="mx-auto mt-12 max-w-md" />
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
