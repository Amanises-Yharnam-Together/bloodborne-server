"use client";

import Image from "next/image";
import Link from "next/link";
import { AnimatePresence, motion, useReducedMotion } from "motion/react";
import { useState } from "react";

import { cn } from "@/lib/utils";

export type StartStep = {
  title: string;
  text: string;
  href: string;
  src: string;
  width: number;
  height: number;
  alt: string;
};

const numerals = ["I", "II", "III", "IV", "V"];

/**
 * The getting-started steps as a list beside one picture: choosing a step (click, tap or keys)
 * shows the screen that step is about, faded in with Motion.
 */
export function StartSteps({ steps }: { steps: StartStep[] }) {
  const [active, setActive] = useState(0);
  const reduce = useReducedMotion();
  const step = steps[active];
  return (
    <div className="grid items-start gap-x-14 gap-y-8 lg:grid-cols-[minmax(0,5fr)_minmax(0,7fr)]">
      <ol className="space-y-3" role="tablist" aria-label="Steps" aria-orientation="vertical">
        {steps.map((s, i) => (
          <li key={s.title}>
            <button
              type="button"
              role="tab"
              id={`start-tab-${i}`}
              aria-selected={i === active}
              aria-controls="start-panel"
              onClick={() => setActive(i)}
              onKeyDown={(e) => {
                if (e.key === "ArrowDown" || e.key === "ArrowRight")
                  setActive((active + 1) % steps.length);
                if (e.key === "ArrowUp" || e.key === "ArrowLeft")
                  setActive((active + steps.length - 1) % steps.length);
              }}
              className={cn(
                "group flex w-full cursor-pointer gap-5 border-l-2 py-4 pr-4 pl-5 text-left transition-colors",
                i === active
                  ? "panel border-l-title"
                  : "border-l-silver/20 hover:border-l-silver/60 hover:bg-white/[0.02]",
              )}
            >
              <span
                className={cn(
                  "w-12 shrink-0 text-4xl leading-none",
                  i === active ? "text-title" : "text-title/50 group-hover:text-title/80",
                )}
              >
                {numerals[i]}
              </span>
              <span>
                <span className="block text-2xl text-bone">{s.title}</span>
                <span className="mt-1 block text-muted-foreground">{s.text}</span>
              </span>
            </button>
          </li>
        ))}
      </ol>
      <div id="start-panel" role="tabpanel" aria-labelledby={`start-tab-${active}`}>
        <div className="relative">
          <AnimatePresence mode="wait" initial={false}>
            <motion.figure
              key={step.src}
              initial={reduce ? false : { opacity: 0, y: 12 }}
              animate={{ opacity: 1, y: 0 }}
              exit={reduce ? undefined : { opacity: 0, y: -8 }}
              transition={{ duration: 0.35, ease: "easeOut" }}
            >
              <Image
                src={step.src}
                alt={step.alt}
                width={step.width}
                height={step.height}
                sizes="(min-width: 1024px) 640px, 100vw"
                className="h-auto w-full border-y border-silver/30 shadow-[0_30px_80px_rgb(0_0_0/0.7)]"
              />
              <figcaption className="mt-3 flex flex-wrap items-baseline justify-between gap-2 text-base text-muted-foreground italic">
                <span>{step.alt}</span>
                <Link
                  href={step.href}
                  className="text-title not-italic underline decoration-title/40 underline-offset-4 hover:text-bone"
                >
                  In the guide
                </Link>
              </figcaption>
            </motion.figure>
          </AnimatePresence>
        </div>
      </div>
    </div>
  );
}
