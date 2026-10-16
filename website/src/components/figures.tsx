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

/* Per-channel traffic from the phase 0 packet trace (docs/NATIVE_NETCODE.md): the host of the
   hit3 rig scenario, three players, 235 s. */
export const channelTraffic = [
  { ch: "0x06", name: "player state", out: 2592, in: 2439 },
  { ch: "0x05", name: "enemy HP", out: 3289, in: 0 },
  { ch: "0x04", name: "enemy positions", out: 1181, in: 1484 },
  { ch: "0x31", name: "event script sync", out: 554, in: 662 },
  { ch: "0x13", name: "character data", out: 405, in: 382 },
  { ch: "0x29", name: "join catch-up", out: 362, in: 0 },
  { ch: "0x16/17", name: "near lists", out: 127, in: 77 },
  { ch: "0x3D", name: "enemy AI hand-over", out: 20, in: 95 },
  { ch: "0x34", name: "enemy hand-over block", out: 0, in: 58 },
];

const SENT = "#b08a42";
const RECEIVED = "#c4443f";

export function ChannelTrafficChart() {
  const max = 3500;
  const rowH = 34;
  const barH = 11;
  const left = 170;
  const width = 640;
  const plot = width - left - 56;
  const top = 28;
  const height = top + channelTraffic.length * rowH + 30;
  const x = (v: number) => left + (v / max) * plot;
  const ticks = [0, 1000, 2000, 3000];
  return (
    <div className="frame bg-card p-4">
      <div className="flex flex-wrap items-center justify-between gap-3">
        <p className="font-display text-xs uppercase tracking-[0.18em] text-gilt">
          Packets per channel, host, 3 players, 235 s
        </p>
        <ul className="flex gap-4 text-sm text-muted-foreground" aria-label="Legend">
          <li className="flex items-center gap-2">
            <span className="h-2.5 w-4 rounded-sm" style={{ background: SENT }} /> Sent
          </li>
          <li className="flex items-center gap-2">
            <span className="h-2.5 w-4 rounded-sm" style={{ background: RECEIVED }} /> Received
          </li>
        </ul>
      </div>
      <div className="-mx-4 overflow-x-auto px-4">
        <svg
          viewBox={`0 0 ${width} ${height}`}
          className="mt-3 w-full min-w-[34rem]"
          role="img"
          aria-label="Bar chart of packets sent and received per game channel by the host; the data is in the table below."
        >
          {ticks.map((t) => (
            <g key={t}>
              <line x1={x(t)} x2={x(t)} y1={top - 8} y2={height - 24} stroke="var(--border)" />
              <text
                x={x(t)}
                y={height - 8}
                textAnchor="middle"
                fontSize="12"
                fill="var(--muted-foreground)"
              >
                {t.toLocaleString("en")}
              </text>
            </g>
          ))}
          {channelTraffic.map((d, i) => {
            const y = top + i * rowH;
            const bar = (v: number, dy: number, color: string, label: string) =>
              v > 0 ? (
                <g>
                  <title>{`${d.ch} ${d.name}: ${v.toLocaleString("en")} ${label}`}</title>
                  <rect
                    x={left}
                    y={y + dy}
                    width={Math.max(x(v) - left, 2)}
                    height={barH}
                    rx="2"
                    fill={color}
                  />
                  <text
                    x={x(v) + 6}
                    y={y + dy + barH - 1}
                    fontSize="11.5"
                    fill="var(--muted-foreground)"
                  >
                    {v.toLocaleString("en")}
                  </text>
                </g>
              ) : null;
            return (
              <g key={d.ch}>
                <text
                  x={left - 12}
                  y={y + 11}
                  textAnchor="end"
                  fontSize="13"
                  fill="var(--bone)"
                  fontFamily="var(--font-mono)"
                >
                  {d.ch}
                </text>
                <text
                  x={left - 12}
                  y={y + 25}
                  textAnchor="end"
                  fontSize="11.5"
                  fill="var(--muted-foreground)"
                >
                  {d.name}
                </text>
                {bar(d.out, 1, SENT, "sent")}
                {bar(d.in, 1 + barH + 2, RECEIVED, "received")}
              </g>
            );
          })}
        </svg>
      </div>
    </div>
  );
}

/* The path of one game packet with the server netcode. */
export function PacketPathFigure() {
  const steps = [
    {
      title: "Game sends",
      text: "one send function sees every packet (0x017894E0)",
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
  "channel-traffic": ChannelTrafficChart,
  "packet-path": PacketPathFigure,
  "save-split": SaveSplitFigure,
  "join-flow": JoinFlowFigure,
} as const;

export type FigureId = keyof typeof figures;
