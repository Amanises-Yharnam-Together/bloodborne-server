"use client";

import Image from "next/image";
import { useState } from "react";

import { site, youtubeWatchUrl } from "@/lib/site";

/**
 * The trailer in the site's own frame: a poster from the game with a play button, which swaps in
 * YouTube's player (privacy-enhanced mode) and starts the video. Nothing loads from YouTube until
 * then; without JavaScript the button is a plain link to the video.
 */
export function TrailerPlayer({ id }: { id: string }) {
  const [playing, setPlaying] = useState(false);
  const title = `${site.name} trailer`;
  return (
    <figure className="panel group relative mx-auto aspect-video w-full max-w-4xl overflow-hidden">
      {playing ? (
        <iframe
          className="absolute inset-0 size-full"
          src={`https://www.youtube-nocookie.com/embed/${id}?autoplay=1&rel=0&modestbranding=1`}
          title={title}
          allow="accelerometer; autoplay; clipboard-write; encrypted-media; gyroscope; picture-in-picture; web-share"
          referrerPolicy="strict-origin-when-cross-origin"
          allowFullScreen
        />
      ) : (
        <a
          href={youtubeWatchUrl(id)}
          onClick={(event) => {
            event.preventDefault();
            setPlaying(true);
          }}
          className="absolute inset-0 block focus-visible:outline-none"
          aria-label={`Play the ${title}`}
        >
          <Image
            src="/docs/wipe-lamp.webp"
            alt=""
            fill
            sizes="(min-width: 896px) 896px, 100vw"
            className="object-cover brightness-[0.72] transition duration-500 group-hover:brightness-[0.85]"
          />
          <span className="absolute inset-0 bg-[radial-gradient(ellipse_at_center,transparent_30%,rgb(0_0_0/0.6))]" />
          <span className="absolute inset-0 flex flex-col items-center justify-center gap-4">
            <span className="flex size-20 items-center justify-center rounded-full border border-silver/70 bg-black/55 text-title shadow-[0_0_40px_var(--glow)] transition duration-300 group-hover:scale-105 group-hover:border-title group-hover:text-bone group-focus-visible:border-title sm:size-24">
              <svg viewBox="0 0 24 24" aria-hidden="true" className="ml-1 size-8 sm:size-10">
                <path d="M7 4.5v15l12-7.5z" fill="currentColor" />
              </svg>
            </span>
            <span className="text-xl text-bone tracking-wide sm:text-2xl">Watch the trailer</span>
          </span>
        </a>
      )}
    </figure>
  );
}
