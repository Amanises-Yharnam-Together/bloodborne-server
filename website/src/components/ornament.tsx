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
    "h-px flex-1 bg-[linear-gradient(90deg,rgb(185_180_170/0.15),rgb(220_215_205/0.75),rgb(185_180_170/0.15))]";
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

/** A menu's title in old gold between two rules, like the game's "System" or "Inventory". */
export function MenuTitle({
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
    <div className={cn("mx-auto max-w-3xl", className)}>
      <MenuRule />
      <Tag
        id={id}
        className="scroll-mt-24 py-2 text-center text-2xl font-normal text-title sm:text-3xl"
      >
        {children}
      </Tag>
      <div className="h-px bg-[linear-gradient(90deg,transparent,rgb(185_180_170/0.45),transparent)]" />
    </div>
  );
}

/** A pad button glyph as the game's key hints draw them. */
export function PadGlyph({ button }: { button: "cross" | "circle" | "triangle" | "square" }) {
  return (
    <svg
      viewBox="0 0 20 20"
      className="inline-block size-[1.05em] align-[-0.15em]"
      fill="none"
      strokeWidth="1.6"
      aria-hidden
    >
      <circle cx="10" cy="10" r="9" fill="#1b1a18" stroke="#6f6b64" strokeWidth="1" />
      {button === "cross" ? (
        <path d="M6.5 6.5l7 7M13.5 6.5l-7 7" stroke="#8fb4e3" />
      ) : button === "circle" ? (
        <circle cx="10" cy="10" r="4" stroke="#e38f8f" />
      ) : button === "triangle" ? (
        <path d="M10 5.5l4.5 8h-9z" stroke="#8fe3c6" />
      ) : (
        <rect x="6" y="6" width="8" height="8" stroke="#e3a8d8" />
      )}
    </svg>
  );
}
