import { cn } from "@/lib/utils";

/** A menu rule with a small lozenge in the middle, like the dividers in the game's menus. */
export function Ornament({ className }: { className?: string }) {
  return (
    <div className={cn("flex items-center gap-3 text-gilt", className)} aria-hidden>
      <span className="h-px flex-1 bg-[linear-gradient(90deg,transparent,currentColor)] opacity-60" />
      <span className="size-1.5 rotate-45 border border-current" />
      <span className="size-2 rotate-45 bg-current opacity-80" />
      <span className="size-1.5 rotate-45 border border-current" />
      <span className="h-px flex-1 bg-[linear-gradient(270deg,transparent,currentColor)] opacity-60" />
    </div>
  );
}

export function SectionHeading({
  eyebrow,
  title,
  children,
  id,
  className,
}: {
  eyebrow: string;
  title: string;
  children?: React.ReactNode;
  id?: string;
  className?: string;
}) {
  return (
    <div className={cn("mx-auto max-w-2xl text-center", className)}>
      <p className="eyebrow">{eyebrow}</p>
      <h2
        id={id}
        className="mt-3 scroll-mt-24 font-display text-3xl tracking-[0.04em] text-bone sm:text-4xl"
      >
        {title}
      </h2>
      <Ornament className="mx-auto mt-5 max-w-56" />
      {children ? <div className="mt-5 text-lg text-muted-foreground">{children}</div> : null}
    </div>
  );
}
