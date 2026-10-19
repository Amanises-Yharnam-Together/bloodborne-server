"use client";

import Image from "next/image";
import { AnimatePresence, motion, useReducedMotion } from "motion/react";
import { useId, useState } from "react";

import { InlineText } from "@/components/inline";
import type { GalleryItem } from "@/content/docs";
import { cn } from "@/lib/utils";

/**
 * Several screenshots of one screen or one flow, one at a time: a row of tabs (click, tap or
 * arrow keys) above the picture, which fades over with Motion.
 */
export function ImageTabs({ items }: { items: GalleryItem[] }) {
  const [active, setActive] = useState(0);
  const reduce = useReducedMotion();
  const id = useId();
  const item = items[active];
  const move = (by: number) => setActive((active + by + items.length) % items.length);
  return (
    <div className="my-8">
      <div
        role="tablist"
        aria-label="Screenshots"
        className="mb-3 flex flex-wrap gap-x-1 gap-y-2 border-b border-silver/20"
      >
        {items.map((it, i) => (
          <button
            key={it.src}
            type="button"
            role="tab"
            id={`${id}-tab-${i}`}
            aria-selected={i === active}
            aria-controls={`${id}-panel`}
            tabIndex={i === active ? 0 : -1}
            onClick={() => setActive(i)}
            onKeyDown={(e) => {
              if (e.key === "ArrowRight" || e.key === "ArrowDown") move(1);
              if (e.key === "ArrowLeft" || e.key === "ArrowUp") move(-1);
            }}
            className={cn(
              "-mb-px cursor-pointer border-b-2 px-3 py-1.5 text-base transition-colors",
              i === active
                ? "border-title text-bone"
                : "border-transparent text-muted-foreground hover:text-bone",
            )}
          >
            <span className="mr-1.5 text-title/70">{i + 1}</span>
            {it.label}
          </button>
        ))}
      </div>
      <div id={`${id}-panel`} role="tabpanel" aria-labelledby={`${id}-tab-${active}`}>
        <AnimatePresence mode="wait" initial={false}>
          <motion.figure
            key={item.src}
            initial={reduce ? false : { opacity: 0 }}
            animate={{ opacity: 1 }}
            exit={reduce ? undefined : { opacity: 0 }}
            transition={{ duration: 0.25, ease: "easeOut" }}
          >
            <Image
              src={item.src}
              alt={item.alt}
              width={item.width}
              height={item.height}
              sizes="(min-width: 1024px) 832px, 100vw"
              className="mx-auto h-auto w-full border-y border-silver/30 bg-card shadow-[0_20px_60px_rgb(0_0_0/0.6)]"
              style={{ maxWidth: item.width }}
            />
            <figcaption className="mt-3 text-sm text-muted-foreground italic">
              <InlineText text={item.caption} />
            </figcaption>
          </motion.figure>
        </AnimatePresence>
      </div>
    </div>
  );
}
