import { cn } from "@/lib/utils";

function Box({
  title,
  lines,
  accent = false,
  className,
}: {
  title: string;
  lines: string[];
  accent?: boolean;
  className?: string;
}) {
  return (
    <div
      className={cn(
        "frame bg-card px-4 py-3 text-left",
        accent &&
          "border-blood-bright/50 bg-[linear-gradient(180deg,rgb(122_23_23/0.18),transparent)]",
        className,
      )}
    >
      <p className="font-display text-xs uppercase tracking-[0.18em] text-gilt">{title}</p>
      <ul className="mt-2 space-y-1 text-[0.95rem] leading-snug text-muted-foreground">
        {lines.map((l) => (
          <li key={l}>{l}</li>
        ))}
      </ul>
    </div>
  );
}

/** Games around the server: the shape of the server netcode, drawn with HTML so it reads on
 *  every screen and costs no image. */
export function ArchitectureDiagram() {
  const game = ["Bloodborne PC, the 1.09 game code", "co-op hooks at the packet choke point"];
  return (
    <figure
      className="mx-auto grid max-w-4xl items-center gap-4 md:grid-cols-[1fr_auto_1.3fr_auto_1fr]"
      aria-label="Every game keeps one UDP link to the server; the games never talk to each other."
    >
      <div className="grid gap-3">
        <Box title="Hunter 1" lines={game} />
        <Box title="Hunter 2" lines={game} />
      </div>
      <Connector />
      <Box
        accent
        title="Co-op server"
        className="py-5"
        lines={[
          "accounts, presence, the team lobby",
          "the session: members, places, one enemy owner per map",
          "the world log: ordered, saved to disk, snapshots for joiners",
          "the relay: game packets to the right members",
        ]}
      />
      <Connector />
      <div className="grid gap-3">
        <Box title="Hunter 3" lines={game} />
        <Box title="Hunter 4" lines={game} />
      </div>
      <figcaption className="text-center text-sm text-muted-foreground md:col-span-5">
        One UDP link per game, with its own resends and ordering. No game talks to another.
      </figcaption>
    </figure>
  );
}

function Connector() {
  return (
    <div className="flex flex-col items-center justify-center text-gilt md:flex-row" aria-hidden>
      <span className="h-8 w-px bg-gilt/50 md:h-px md:w-10" />
      <span className="py-1 font-display text-[0.65rem] uppercase tracking-[0.2em] md:px-1.5">
        UDP
      </span>
      <span className="h-8 w-px bg-gilt/50 md:h-px md:w-10" />
    </div>
  );
}
