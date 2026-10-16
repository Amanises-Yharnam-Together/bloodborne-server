// The docs' diagrams and charts: server-rendered SVG and HTML, no images to download and no
// client JavaScript. Each one has a text alternative in src/content/docs.ts.

import { ArchitectureDiagram } from "@/components/architecture-diagram";
import { cn } from "@/lib/utils";

function Hunter({ x, y, n }: { x: number; y: number; n: number }) {
  return (
    <g>
      <circle cx={x} cy={y} r="18" fill="var(--card)" stroke="var(--gilt)" strokeOpacity="0.7" />
      <text
        x={x}
        y={y + 5}
        textAnchor="middle"
        fontSize="13"
        fill="var(--bone)"
        fontFamily="var(--font-display)"
      >
        {n}
      </text>
    </g>
  );
}

function Panel({
  title,
  note,
  children,
}: {
  title: string;
  note: string;
  children: React.ReactNode;
}) {
  return (
    <div className="frame bg-card p-4">
      <p className="text-center font-display text-xs uppercase tracking-[0.18em] text-gilt">
        {title}
      </p>
      <svg viewBox="0 0 240 200" className="mx-auto mt-2 w-full max-w-72" aria-hidden>
        {children}
      </svg>
      <p className="text-center text-sm text-muted-foreground">{note}</p>
    </div>
  );
}

/* Peer to peer against one server: the links each design needs for four hunters. */
export function TopologyFigure() {
  const nodes = [
    { x: 50, y: 40 },
    { x: 190, y: 40 },
    { x: 190, y: 160 },
    { x: 50, y: 160 },
  ];
  const mesh: [number, number][] = [
    [0, 1],
    [0, 2],
    [0, 3],
    [1, 2],
    [1, 3],
    [2, 3],
  ];
  const hub = { x: 120, y: 100 };
  return (
    <div className="grid gap-4 sm:grid-cols-2">
      <Panel
        title="Before: peer to peer"
        note="6 links for 4 hunters; the world lives in the host's game"
      >
        {mesh.map(([a, b]) => (
          <line
            key={`${a}${b}`}
            x1={nodes[a].x}
            y1={nodes[a].y}
            x2={nodes[b].x}
            y2={nodes[b].y}
            stroke="var(--muted-foreground)"
            strokeOpacity="0.6"
            strokeDasharray="4 4"
          />
        ))}
        {nodes.map((p, i) => (
          <Hunter key={i} {...p} n={i + 1} />
        ))}
      </Panel>
      <Panel title="After: one server" note="4 links for 4 hunters; the world lives on the server">
        {nodes.map((p, i) => (
          <line
            key={i}
            x1={p.x}
            y1={p.y}
            x2={hub.x}
            y2={hub.y}
            stroke="var(--gilt)"
            strokeOpacity="0.8"
          />
        ))}
        <rect
          x={hub.x - 30}
          y={hub.y - 18}
          width="60"
          height="36"
          fill="var(--primary)"
          stroke="var(--gilt)"
        />
        <text
          x={hub.x}
          y={hub.y + 4}
          textAnchor="middle"
          fontSize="11"
          fill="var(--bone)"
          fontFamily="var(--font-display)"
          letterSpacing="1.5"
        >
          SERVER
        </text>
        {nodes.map((p, i) => (
          <Hunter key={i} {...p} n={i + 1} />
        ))}
      </Panel>
    </div>
  );
}

/* How many links a team keeps alive: every pair of players peer to peer, one per player with a
   server. */
export const linkCounts = [2, 3, 4].map((n) => ({ players: n, p2p: (n * (n - 1)) / 2, server: n }));

const P2P = "#b08a42";
const SERVER = "#c4443f";

export function LinksChart() {
  const max = 6;
  const width = 520;
  const height = 240;
  const left = 36;
  const bottom = 200;
  const top = 20;
  const groupW = (width - left - 10) / linkCounts.length;
  const barW = 34;
  const y = (v: number) => bottom - (v / max) * (bottom - top);
  return (
    <div className="frame bg-card p-4">
      <div className="flex flex-wrap items-center justify-between gap-3">
        <p className="font-display text-xs uppercase tracking-[0.18em] text-gilt">
          Links per team size
        </p>
        <ul className="flex gap-4 text-sm text-muted-foreground" aria-label="Legend">
          <li className="flex items-center gap-2">
            <span className="h-2.5 w-4 rounded-sm" style={{ background: P2P }} /> Peer to peer
          </li>
          <li className="flex items-center gap-2">
            <span className="h-2.5 w-4 rounded-sm" style={{ background: SERVER }} /> Server
          </li>
        </ul>
      </div>
      <svg
        viewBox={`0 0 ${width} ${height}`}
        className="mx-auto mt-3 w-full max-w-xl"
        role="img"
        aria-label="Links per team size: 2 players, 1 peer to peer and 2 with a server; 3 players, 3 and 3; 4 players, 6 and 4."
      >
        {[0, 2, 4, 6].map((t) => (
          <g key={t}>
            <line x1={left} x2={width - 10} y1={y(t)} y2={y(t)} stroke="var(--border)" />
            <text
              x={left - 10}
              y={y(t) + 4}
              textAnchor="end"
              fontSize="12"
              fill="var(--muted-foreground)"
            >
              {t}
            </text>
          </g>
        ))}
        {linkCounts.map((d, i) => {
          const cx = left + groupW * i + groupW / 2;
          const bar = (v: number, x: number, color: string, label: string) => (
            <g>
              <title>{`${d.players} players, ${label}: ${v} ${v === 1 ? "link" : "links"}`}</title>
              <rect x={x} y={y(v)} width={barW} height={bottom - y(v)} rx="2" fill={color} />
              <text
                x={x + barW / 2}
                y={y(v) - 6}
                textAnchor="middle"
                fontSize="12"
                fill="var(--muted-foreground)"
              >
                {v}
              </text>
            </g>
          );
          return (
            <g key={d.players}>
              {bar(d.p2p, cx - barW - 1, P2P, "peer to peer")}
              {bar(d.server, cx + 1, SERVER, "server")}
              <text x={cx} y={bottom + 22} textAnchor="middle" fontSize="13" fill="var(--bone)">
                {d.players} players
              </text>
            </g>
          );
        })}
      </svg>
    </div>
  );
}

/* The path of one game packet with the server netcode. */
export function PacketPathFigure() {
  const steps = [
    {
      title: "Game sends",
      text: "one send point sees every packet",
    },
    { title: "Channel policy", text: "pass, same map only, ours, or drop" },
    {
      title: "Our UDP link",
      text: "sequence, ack, resend, in order for reliable channels",
    },
    {
      title: "Server relays",
      text: "to the members who need it; it never parses game packets",
    },
    {
      title: "Game receives",
      text: "injected into the receiver's per-member queue",
    },
  ];
  return (
    <ol className="grid gap-3 sm:grid-cols-5" aria-label="The path of a game packet">
      {steps.map((s, i) => (
        <li
          key={s.title}
          className={cn("frame relative bg-card px-4 py-3", i === 3 && "bg-primary/25")}
        >
          <span className="font-display text-xs text-gilt">{String(i + 1).padStart(2, "0")}</span>
          <p className="mt-1 font-display text-sm tracking-[0.06em] text-bone">{s.title}</p>
          <p className="mt-1 text-sm leading-snug text-muted-foreground">{s.text}</p>
        </li>
      ))}
    </ol>
  );
}

/* What stays in a player's save and what the team's world on the server holds. */
export function SaveSplitFigure() {
  const columns = [
    {
      title: "Your save, on your PC",
      sub: "the character",
      items: [
        "level and stats",
        "gear and inventory",
        "Insight and echoes",
        "appearance",
        "NPC gifts you received",
      ],
    },
    {
      title: "The team world, on the server",
      sub: "one per team, saved to disk",
      items: [
        "bosses beaten",
        "lamps, doors, levers, shortcuts",
        "items picked up",
        "NPC and quest states",
        "kills and map state",
      ],
      accent: true,
    },
  ];
  return (
    <div className="grid gap-4 sm:grid-cols-2">
      {columns.map((c) => (
        <div key={c.title} className={cn("frame bg-card px-5 py-4", c.accent && "bg-primary/20")}>
          <p className="font-display text-sm uppercase tracking-[0.14em] text-bone">{c.title}</p>
          <p className="text-sm text-gilt">{c.sub}</p>
          <ul className="mt-3 space-y-1 text-base text-muted-foreground">
            {c.items.map((i) => (
              <li key={i}>{i}</li>
            ))}
          </ul>
        </div>
      ))}
    </div>
  );
}

/* From the title screen to one world. */
export function JoinFlowFigure() {
  const steps = [
    "Continue, Load Game or New Game",
    "Team screen: create a team or accept an invite",
    "Everyone picks a character and marks Ready",
    "The leader begins the hunt",
    "Loading holds until the team is together",
    "One world, each hunter where they stand",
  ];
  return (
    <ol className="relative grid gap-3 border-l border-gilt/40 pl-6" aria-label="Joining a team">
      {steps.map((s, i) => (
        <li key={s} className="relative">
          <span
            aria-hidden
            className="absolute top-2 -left-[1.95rem] size-2.5 rotate-45 border border-gilt bg-background"
          />
          <span className="font-display text-xs text-gilt">{String(i + 1).padStart(2, "0")}</span>{" "}
          <span className="text-base text-foreground">{s}</span>
        </li>
      ))}
    </ol>
  );
}

export const figures = {
  architecture: ArchitectureDiagram,
  topology: TopologyFigure,
  links: LinksChart,
  "packet-path": PacketPathFigure,
  "save-split": SaveSplitFigure,
  "join-flow": JoinFlowFigure,
} as const;

export type FigureId = keyof typeof figures;
