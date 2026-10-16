import { cn } from "@/lib/utils";

/* The scrollwork at the ends and in the middle of a menu rule, drawn for this site after the
   shape of the game's menu dividers (not traced from them). */
function EndCap({ flip }: { flip?: boolean }) {
  return (
    <svg
      viewBox="0 0 44 16"
      className={cn("h-3 w-auto shrink-0", flip && "-scale-x-100")}
      fill="none"
      stroke="currentColor"
      strokeLinecap="round"
      aria-hidden
    >
      <path d="M44 8H22c-5 0-8-3-6.5-5.5S21 1 21 4s-3 4-5 4" strokeWidth="1.1" />
      <path d="M22 8c-5 0-8 3-6.5 5.5S21 15 21 12" strokeWidth="1.1" />
      <circle cx="9" cy="8" r="1.4" fill="currentColor" stroke="none" />
      <circle cx="3.5" cy="8" r="0.9" fill="currentColor" stroke="none" />
    </svg>
  );
}

function Crest() {
  return (
    <svg
      viewBox="0 0 120 22"
      className="h-4 w-auto shrink-0"
      fill="none"
      stroke="currentColor"
      strokeLinecap="round"
      aria-hidden
    >
      <path d="M60 3l5 8-5 8-5-8z" fill="currentColor" fillOpacity="0.85" stroke="none" />
      <g strokeWidth="1.1">
        <path d="M66 11c4-7 13-8 15-3 1.6 4-3.5 6-5 3s2-4 3-2" />
        <path d="M66 11c4 5 10 6 13 4" />
        <path d="M81 11h22" />
        <path d="M54 11c-4-7-13-8-15-3-1.6 4 3.5 6 5 3s-2-4-3-2" />
        <path d="M54 11c-4 5-10 6-13 4" />
        <path d="M39 11H17" />
      </g>
      <circle cx="108" cy="11" r="1.5" fill="currentColor" stroke="none" />
      <circle cx="12" cy="11" r="1.5" fill="currentColor" stroke="none" />
    </svg>
  );
}

/** A menu rule: a silver line with scrollwork ends and, when `crest`, a crest in the middle. */
export function MenuRule({ crest = true, className }: { crest?: boolean; className?: string }) {
  const line =
    "h-px flex-1 bg-[linear-gradient(90deg,rgb(201_168_106/0.15),rgb(217_184_120/0.75),rgb(201_168_106/0.15))]";
  return (
    <div className={cn("flex items-center gap-1.5 text-silver", className)} aria-hidden>
      <EndCap />
      <span className={line} />
      {crest ? (
        <>
          <Crest />
          <span className={line} />
        </>
      ) : null}
      <EndCap flip />
    </div>
  );
}

/** A section heading: the title in old gold, left-aligned, over a short rule with a scroll end. */
export function Heading({
  children,
  id,
  as: Tag = "h2",
  className,
}: {
  children: React.ReactNode;
  id?: string;
  as?: "h1" | "h2";
  className?: string;
}) {
  return (
    <div className={className}>
      <Tag
        id={id}
        className={cn(
          "scroll-mt-24 font-normal text-title",
          Tag === "h1" ? "text-4xl sm:text-5xl" : "text-xl tracking-[0.16em] uppercase sm:text-2xl",
        )}
      >
        {children}
      </Tag>
      <div className="mt-2.5 flex items-center gap-2 text-title" aria-hidden>
        <span className="h-px w-24 bg-[linear-gradient(90deg,transparent,rgb(217_184_120/0.8))]" />
        <span className="size-1.5 rotate-45 bg-current" />
        <span className="h-px w-24 bg-[linear-gradient(90deg,rgb(217_184_120/0.8),transparent)]" />
      </div>
    </div>
  );
}
