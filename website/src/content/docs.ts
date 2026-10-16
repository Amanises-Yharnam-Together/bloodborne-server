// The documentation, as data. The pages render it, and /llms-full.txt prints the same text as
// Markdown, so people and machines read one source. Inline text understands `code`, **bold** and
// [links](/path).

import type { FigureId } from "@/components/figures";
import { RELEASES_URL, REPO_URL, SERVER_COOP_BRANCH_URL } from "@/lib/site";

export type Block =
  | { type: "p"; text: string }
  | { type: "ul"; items: string[] }
  | { type: "ol"; items: string[] }
  | { type: "code"; text: string; lang?: string }
  | { type: "table"; head: string[]; rows: string[][] }
  | { type: "note"; text: string }
  | { type: "h3"; text: string }
  /** A diagram or chart drawn by the site (src/components/figures.tsx); `alt` is its text
   *  version for screen readers and /llms-full.txt. */
  | { type: "figure"; figure: FigureId; caption: string; alt: string }
  /** A screenshot in public/; width and height are the file's own. */
  | {
      type: "image";
      src: string;
      alt: string;
      caption: string;
      width: number;
      height: number;
    };

export type Section = { id: string; title: string; blocks: Block[] };

export type DocPage = {
  slug: string;
  title: string;
  /** One or two sentences: the page's meta description and its line in /llms.txt. */
  description: string;
  sections: Section[];
};

const blob = (path: string, branch = "main") => `${REPO_URL}/blob/${branch}/${path}`;

const install: DocPage = {
  slug: "install",
  title: "Install and play",
  description:
    "What you need to play Bloodborne Seamless Co-op, how to install the release, and how the players of a party reach the host's PC.",
  sections: [
    {
      id: "requirements",
      title: "What you need",
      blocks: [
        {
          type: "ul",
          items: [
            "**Your own copy of Bloodborne for PS4, version 1.09.** No game files are included: use a dump of the game folder with the 1.09 update, or the PS4 `.pkg` files, which the included launcher installs (**Install from PKG**).",
            "Windows 10 (1903 or later) or Windows 11, 64-bit.",
            "A graphics card with Vulkan 1.3 and a current driver.",
            "Everyone in a party on **the same release**. The server refuses a different build with a clear message: there is no mixed-version play.",
          ],
        },
        {
          type: "p",
          text: "It is tested with CUSA03173 (the European Game of the Year edition) at 1.09. All players in a party should use the same edition.",
        },
      ],
    },
    {
      id: "install",
      title: "Install",
      blocks: [
        {
          type: "ol",
          items: [
            `Download the setup \`.exe\` of the latest release from the [download page](/download) (or [GitHub releases](${RELEASES_URL})) and run it. It installs for your Windows user; keep **Create a desktop shortcut** ticked.`,
            "Open **Bloodborne Seamless Co-op**. The launcher finds your Bloodborne folder, or extracts the game and the update from `.pkg` files.",
            "Fill in the **Co-op** card: host (this PC runs the co-op server) or player, the host's IP, an account and the party password.",
            "Press **PLAY**. On the host the co-op server starts by itself. Windows asks once to let co-op through its firewall.",
          ],
        },
        {
          type: "note",
          text: "Online play is locked to 60 FPS. The game's send rate follows its frame rate, so every player runs at the same speed.",
        },
      ],
    },
    {
      id: "network",
      title: "Reach the host",
      blocks: [
        {
          type: "p",
          text: "Everything runs on the players' own PCs: one player hosts the server, nothing is in the cloud. Every other player must reach the host's PC, either:",
        },
        {
          type: "ul",
          items: [
            "on the same VPN, such as Radmin VPN, Tailscale or ZeroTier (the easy way), or",
            "at the host's public IP with these ports forwarded: TCP **31313**, **31315** and **20443**, and UDP **31314**.",
          ],
        },
        {
          type: "p",
          text: "With the server netcode the UDP port carries every game's co-op traffic, because the games never talk to each other directly. It must be open.",
        },
      ],
    },
    {
      id: "play",
      title: "Play together",
      blocks: [
        {
          type: "p",
          text: "In today's release everyone loads their character and is pulled into the host's world a few seconds later, wherever they are, for up to three players.",
        },
        {
          type: "p",
          text: "With the server netcode (in testing, see [the server netcode](/docs/netcode)) teams are made in the game's own menus:",
        },
        {
          type: "ol",
          items: [
            "Continue, Load Game or New Game opens the team screen.",
            "Make a team of up to four from the hunters online, or accept an invite.",
            "Everyone picks a character and marks Ready; the leader presses **Begin the hunt**.",
            "Every game loads its own save, the loading screen stays up until the team is together, and you are in one world where each of you stands.",
          ],
        },
        {
          type: "image",
          src: "/docs/team-screen.webp",
          width: 980,
          height: 740,
          alt: "The Co-op Team page in the game's own menu style: Team, Invite, Invitations, Members, Remove and Start, showing a team of 2 of 4.",
          caption: "The team page, drawn with the game's own menus (test rig accounts).",
        },
        {
          type: "image",
          src: "/docs/team-invitation.webp",
          width: 980,
          height: 740,
          alt: "An invitation on the team page with the choices to join the host's team or decline.",
          caption: "Accepting an invitation to a team.",
        },
        {
          type: "p",
          text: "This is a fan project, played regularly in parties of two and three and checked by an automated multi-game test rig, but expect rough edges and keep backups of your saves.",
        },
      ],
    },
    {
      id: "help",
      title: "Problems",
      blocks: [
        {
          type: "p",
          text: `Report a bug on [GitHub issues](${REPO_URL}/issues). The launcher writes crash reports with the dump and the logs; attach them, and say which release every player was on.`,
        },
      ],
    },
  ],
};

const architecture: DocPage = {
  slug: "architecture",
  title: "How it works",
  description:
    "The pieces of Bloodborne Seamless Co-op: the native PC port, the co-op code compiled into it, the patched map events and the self-hosted server.",
  sections: [
    {
      id: "overview",
      title: "Overview",
      blocks: [
        {
          type: "figure",
          figure: "architecture",
          caption: "Every game keeps one UDP link to the co-op server on the host's PC.",
          alt: "Up to four games (Bloodborne PC with the co-op hooks) each connect over UDP to one co-op server, which holds accounts, presence and the team lobby, the session with one enemy owner per map, the world log saved to disk, and the relay of game packets. The games never connect to each other.",
        },
        {
          type: "p",
          text: "Two programs: the game, which every player runs, and the server, which one player runs next to their game. The design above is the server netcode; today's release still links the games peer to peer.",
        },
      ],
    },
    {
      id: "port",
      title: "The port",
      blocks: [
        {
          type: "p",
          text: "**Bloodborne PC** loads the PS4 game's own x86-64 code and runs it on a small runtime, with shadPS4's renderer drawing through Vulkan. There is no emulator in between: hooks, memory, threads, files and networking are direct, which removes a whole class of co-op failures.",
        },
        {
          type: "p",
          text: "It keeps everything the port offers: 60 FPS and above, resolutions up to 4K, DLSS, FSR 3.1 and 4 and XeSS upscaling, frame generation, HDR, ReShade, mouse and keyboard and gamepads. The port is optimised for co-op: the co-op code is part of the build, and online play is locked to 60 FPS so every game sends at the same rate.",
        },
      ],
    },
    {
      id: "coop-code",
      title: "The co-op code",
      blocks: [
        {
          type: "p",
          text: "The co-op code is compiled into the port (`bbport/gpu/bbcoop` wires it in) and changes the game at known places in the 1.09 eboot: about 125 patch sites and hooks, listed in one table (`emulator/src/core/bloodborne_patch_sites.h`) with a byte check each. The launcher warns when another game patch writes over one of them.",
        },
        {
          type: "ul",
          items: [
            "**Joining without summoning.** The team's host opens its game session itself and sends its join data to the server; each member's game joins that session as soon as it stands in its world. The co-op code removes the limits that would end a normal session (boss kills, lamp trips, deaths, map changes, area restrictions) and puts players back together after loads.",
            "**One world.** Enemies, their HP and their deaths are shared: every player's hits count on the same health bar. Doors, lamps, shortcuts and boss kills belong to the team's world; pickups, echoes and Insight stay each player's own. NPC gifts are per player, so everyone gets their own.",
            "**Party death** (always on, an idea from Elden Ring Nightreign): a killing blow puts you down instead of killing you while a partner stands on your map, and they revive you by striking you. When everyone on a map is down, the party wipes and wakes at its lamps.",
            "**Giving items.** Hand Blood Vials, echoes and other items to a teammate from the inventory; they accept in a native dialog.",
            "**Harder enemies** (optional): HP, damage and poise multipliers for every enemy and boss, to make up for the extra hunters.",
          ],
        },
        {
          type: "image",
          src: "/docs/party-alive.webp",
          width: 1040,
          height: 340,
          alt: "A teammate in the party list: a round framed portrait, the name and a health bar.",
          caption: "The party list: each teammate's portrait, name and health.",
        },
        {
          type: "image",
          src: "/docs/party-downed.webp",
          width: 1040,
          height: 340,
          alt: "The same teammate shown as downed: an empty health bar and a red blood-drop badge on the portrait.",
          caption: "A downed teammate, waiting for a strike to get back up.",
        },
        {
          type: "image",
          src: "/docs/give-items.webp",
          width: 1280,
          height: 720,
          alt: "In game, a guest receives four Blood Vials from the host, shown in the game's own item dialog, with the host's name and health above their head.",
          caption: "Four Blood Vials received from the host.",
        },
      ],
    },
    {
      id: "map-events",
      title: "Map events",
      blocks: [
        {
          type: "p",
          text: "Some of the game's event scripts end early for a summoned player: boss rewards, boss fog, the first visit to the Hunter's Dream. Setup patches copies of **your own** event files (`tools/re/party_boss_events.py`, which checks that only the intended instructions changed). The release carries only their expected hashes, never game files.",
        },
      ],
    },
    {
      id: "services",
      title: "Self-hosted services",
      blocks: [
        {
          type: "p",
          text: "`server/` is a shadNet server with the co-op server added (`server/coop`: the protobuf messages, the world log, the team lobby, trading). The game's own online requests, JSON over HTTP as the PS4 game sends them, are answered by shadNet itself, so the game goes online without the official or community servers and without PlayStation Network.",
        },
      ],
    },
    {
      id: "layout",
      title: "Repository layout",
      blocks: [
        {
          type: "table",
          head: ["Folder", "What it is"],
          rows: [
            [
              "`bbport/`",
              "Bloodborne PC, the native port, with the launcher and the Windows packaging",
            ],
            [
              "`emulator/`",
              "shadPS4 with networking; `src/core/bloodborne_*` is the co-op code the port compiles in",
            ],
            [
              "`server/`",
              "shadNet: accounts, rooms, signaling, and the co-op server (team lobby, team worlds, the relay)",
            ],
            ["`backend/`", "a stand-in for the game's online services"],
            [
              "`tools/`",
              "build scripts, local CI, releases, the multi-game test rig (`tools/rig`), reverse-engineering helpers (`tools/re`)",
            ],
            ["`docs/`", "design notes and handovers"],
            ["`website/`", "this site"],
          ],
        },
        {
          type: "p",
          text: `Every upstream is imported with its full history, so \`git log\` shows its authors. The design notes in the repository go deeper: [BBPORT.md](${blob("docs/BBPORT.md")}) (the port and its co-op wiring), [WORLD_SYNC.md](${blob("docs/WORLD_SYNC.md")}) and [FLAGS.md](${blob("docs/FLAGS.md")}) (who owns each event flag).`,
        },
      ],
    },
  ],
};

const netcode: DocPage = {
  slug: "netcode",
  title: "The server netcode",
  description:
    "Why Bloodborne Seamless Co-op replaced peer-to-peer with a server that owns the team's world: the game's native netcode, the UDP protocol, the reliability layer, team worlds and the native team lobby.",
  sections: [
    {
      id: "why",
      title: "Why a server",
      blocks: [
        {
          type: "p",
          text: "Bloodborne's own netcode assumes one host's world, a short visit and peer-to-peer links. Seamless co-op breaks every one of those assumptions, and patching around them took about 120 patch sites and a set of side channels. The rewrite replaces it: **every game keeps one UDP connection to a server, and nothing goes game to game.**",
        },
        {
          type: "figure",
          figure: "topology",
          caption:
            "Four hunters: six peer-to-peer links and a host who owns the world, or four links to a server that owns it.",
          alt: "Left, peer to peer: four hunters each linked to every other, six links. Right, one server in the middle linked to each of the four hunters, four links.",
        },
        {
          type: "figure",
          figure: "links",
          caption:
            "Links to keep alive as the team grows: every pair of players with peer to peer, one per player with a server.",
          alt: "Bar chart. Two players: 1 link peer to peer, 2 with a server. Three players: 3 and 3. Four players: 6 and 4.",
        },
        {
          type: "table",
          head: ["Question", "Decision", "Why"],
          rows: [
            [
              "Peer to peer or server?",
              "Server",
              "Players reach each other over a VPN anyway, so P2P's one advantage (NAT traversal) is gone. The world survives the host leaving, one place orders every change, and each game has one link instead of a mesh. The cost is one extra hop between guests: milliseconds on a VPN.",
            ],
            [
              "Who owns the world?",
              "The server owns its state; the games still play it",
              "The server records and orders kills, flags, doors, lamps, shortcuts, items, enemy HP and map state, keeps them across sessions and crashes, and hands them to joiners. It runs no game.",
            ],
            [
              "Rewrite movement sync?",
              "No",
              "The game's own player and enemy sync keeps running; its packets just travel through the server.",
            ],
            [
              "Wire format?",
              "UDP; protobuf for our messages, raw bytes for the game's",
              "A schema ends hand-written frames breaking older readers. Game packets are many per second and the server never reads them.",
            ],
          ],
        },
      ],
    },
    {
      id: "native",
      title: "What the game does on its own",
      blocks: [
        {
          type: "p",
          text: "Reverse engineering of the 1.09 eboot, checked with a packet trace on the test rig:",
        },
        {
          type: "ul",
          items: [
            "**Every game packet goes through one send and one receive point.** Packets travel on numbered channels, some unreliable (movement) and some reliable (everything that must arrive). Hooking those two points lets the co-op code see, drop, rewrite or inject any packet.",
            "**The native model is one host's world.** The host runs every enemy and sends their positions and HP; a guest's hit on an enemy goes to the host, which applies it. A joining guest gets the host's flags for the host's current map only, once.",
            "**Players sync themselves.** Each game sends its own player: position, heading, HP and the animation state. A hit on a player goes to the victim's game, which decides its HP.",
          ],
        },
        {
          type: "p",
          text: "When members are on different maps, those assumptions produce double damage, late health bars and joins that only cover one map. That is where the old design's bugs came from.",
        },
      ],
    },
    {
      id: "architecture",
      title: "Architecture",
      blocks: [
        {
          type: "figure",
          figure: "architecture",
          caption: "The server in the middle: what it keeps and what it relays.",
          alt: "Up to four games each connect over one UDP link to the co-op server. The server holds accounts, presence and the team lobby; the session with members, places and one enemy owner per map; the world log, ordered and saved to disk, with snapshots for joiners; and the relay of game packets to the right members.",
        },
        {
          type: "table",
          head: ["Traffic", "Server's role"],
          rows: [
            ["Team and session: lobby, members, who runs which map's enemies", "owns"],
            [
              "World state: kills, flags, doors and levers, lamps, shortcuts, items, enemy HP, map state",
              "owns: orders, stores, settles conflicts, builds snapshots for joiners",
            ],
            ["Player movement, animation and HP", "relays, unread, to members on the same map"],
            [
              "Hits, throws, bullets, forced animations, character data",
              "relays; drops what is meant for members on other maps",
            ],
            ["Enemy movement and state from the map's owner", "relays to the others on that map"],
            ["Enemy damage and HP", "replaced by world-log HP entries, ordered by the server"],
            [
              "Join snapshots",
              "built from the world log, for any map, not only the host's current one",
            ],
          ],
        },
        {
          type: "p",
          text: "Enemies' AI runs in one game per map, chosen by the server: the first member there, handed on when they leave, mid-fight if needed. Each player's character runs in its own game.",
        },
      ],
    },
    {
      id: "protocol",
      title: "The protocol",
      blocks: [
        {
          type: "figure",
          figure: "packet-path",
          caption: "One game packet, from the sending game to the receiving one.",
          alt: "Five steps: the game's send function sees the packet; the channel policy passes it, keeps it to the same map, takes it as ours or drops it; our UDP link adds sequence, acks and resends; the server relays it to the members who need it without parsing it; it is injected into the receiving game's per-member queue.",
        },
        {
          type: "ul",
          items: [
            "**One UDP socket per game** to the server, on a fixed port next to the TCP one. Each datagram holds messages; each message has a one-byte kind and a length.",
            "**Kinds:** `Game` (a small fixed header `{channel, reliable bit, sender member, map, visit}` followed by the game's packet untouched), `Proto` (one protobuf message), `Ack` and `Ping`.",
            "**Our own reliability layer**, per member and per class: sequence numbers, acks, resends and in-order delivery for reliable traffic (the game's reliable channels, world-log entries, snapshots). Unreliable traffic such as movement is never resent; the newest wins. Who is connected, outages and timeouts come from this layer, not from the game's own timers.",
            "**Protobuf messages** (`server/proto/coop.proto`, shared by the game and the server): `Hello` (account, build, protocol), the lobby (`TeamCreate`, `Invite`, `InviteAnswer`, `TeamState`, `Ready`, `Start`), the session (`Place`, `MemberJoined`, `MemberLeft`, `EnemyOwner`), the world log (`Propose`, `Entries`, `SnapshotRequest`, `Snapshot`), vitals, party death and portraits.",
            "**One build for everyone.** `Hello` carries the build, and a different build is refused at the lobby with a clear message. There are no compatibility fallbacks.",
          ],
        },
      ],
    },
    {
      id: "team-worlds",
      title: "Team worlds",
      blocks: [
        {
          type: "figure",
          figure: "save-split",
          caption: "The save splits in two: the character stays yours, the world is the team's.",
          alt: "Your save on your PC holds the character: level and stats, gear and inventory, Insight and echoes, appearance, NPC gifts received. The team world on the server holds bosses beaten, lamps, doors, levers, shortcuts, items picked up, NPC and quest states, kills and map state.",
        },
        {
          type: "ul",
          items: [
            "**The save splits in two.** The character (level, stats, gear, inventory, Insight, echoes, appearance) stays in each player's save on their PC. The world (bosses beaten, lamps, doors, levers, shortcuts, items picked up, NPC and quest states, kills, map state) of a team lives on the server, one world per team.",
            "**The server stores the worlds itself,** with atomic writes and backups. Every accepted world-log entry is written at once, so a crash, a quit or a host change loses nothing, and any member can be the game-level host.",
            "**A new team world starts from the leader's progress.**",
            "**Every save keeps its personal world.** Loading into a team world puts the save's own world aside and applies the team's; playing solo again brings it back. The character is never touched by this.",
            "**Bosses** beaten solo stay in that save's personal world; in a team only the team world counts.",
            "**Up to four** hunters per team: the game's remote player slots.",
          ],
        },
      ],
    },
    {
      id: "lobby",
      title: "The team lobby",
      blocks: [
        {
          type: "p",
          text: "Teams are made in the game's own menus, drawn with its own dialogs and fonts rather than an overlay. Continue, Load Game or New Game opens the team screen: the team and the hunters online, Invite, Ready and Start. When the leader starts, every member's game loads its save; the first to load becomes the game-level host (the server still owns the world) and the others join its session the moment they finish loading. After a crash or a quit, starting the game again rejoins the team where it is.",
        },
        {
          type: "figure",
          figure: "join-flow",
          caption: "From the title screen to one world, with no bell and no signs.",
          alt: "Six steps: Continue, Load Game or New Game; the team screen, to create a team or accept an invite; everyone picks a character and marks Ready; the leader begins the hunt; loading holds until the team is together; one world, each hunter where they stand.",
        },
        {
          type: "image",
          src: "/docs/team-screen.webp",
          width: 980,
          height: 740,
          alt: "The Co-op Team page in the game's own menu style.",
          caption: "The team page in the title menu.",
        },
      ],
    },
    {
      id: "status",
      title: "Status",
      blocks: [
        {
          type: "p",
          text: `The rewrite was built in phases on the [server-coop branch](${SERVER_COOP_BRANCH_URL}), each with tests first and runs on the multi-game rig: a packet trace, a channel policy table, one UDP link to the server, the world log on the server, enemy owners from the server, the native team lobby, the server owning the session (faster joins, host migration, resuming after a drop), one owner per map with a single HP path, server routing with the old peer-to-peer paths deleted, and crash-safe world saves with a server that restarts without ending sessions. It is in testing and ships in the next public release.`,
        },
        {
          type: "p",
          text: `The full research and the plan with every phase are in the repository's design notes on the [server-coop branch](${SERVER_COOP_BRANCH_URL}).`,
        },
      ],
    },
  ],
};

const build: DocPage = {
  slug: "build",
  title: "Build from source",
  description:
    "Build the port, the server and the release of Bloodborne Seamless Co-op on Windows, run the local checks and the multi-game test rig.",
  sections: [
    {
      id: "prerequisites",
      title: "Prerequisites",
      blocks: [
        { type: "p", text: "Windows only for now." },
        {
          type: "ul",
          items: [
            "**The port:** [MSYS2](https://www.msys2.org) with the CLANG64 packages listed in `bbport/packaging/windows/README.md`, and a python.org Python 3.10 or later (it freezes the launcher).",
            "**The server and emulator:** VS 2022 Build Tools (C++ workload, Windows SDK), LLVM 20.1.8 (clang-cl), CMake, Ninja, Qt 6.8.3 `msvc2022_64`.",
            "**Checks:** Python 3.12 with `clang-format==19.1.7` and `reuse`, and [jj](https://jj-vcs.github.io/jj/) for the local CI script.",
          ],
        },
      ],
    },
    {
      id: "clone",
      title: "Get the code",
      blocks: [
        {
          type: "code",
          lang: "powershell",
          text: `git clone ${REPO_URL}.git C:\\bb\\bloodborne-server
cd C:\\bb\\bloodborne-server
git config submodule.emulator/externals/mesa-kosmickrisp.update none   # macOS-only
git submodule update --init --recursive`,
        },
        {
          type: "p",
          text: "`BB_ROOT` (default `C:\\bb`) holds the checkout. Build output goes to `C:\\bb\\out`, outside the tree.",
        },
      ],
    },
    {
      id: "scripts",
      title: "Scripts",
      blocks: [
        {
          type: "table",
          head: ["Script", "What it does"],
          rows: [
            [
              "`tools/build-bbport.ps1 [-Test]`",
              "builds the port and, with `-Test`, its co-op tests",
            ],
            ["`tools/build-server.ps1`", "builds shadNet and its tests"],
            ["`tools/build-emu.ps1`", "builds the emulator and its unit tests"],
            [
              "`tools/ci-check.ps1 -Component <name>`",
              "local CI per component: format, licenses, build, tests",
            ],
            ["`tools/make-release.ps1 -Version x.y.z`", "builds and checks a release"],
            ["`tools/rig/run_scenario.py`", "the automated multi-game test rig"],
          ],
        },
      ],
    },
    {
      id: "release",
      title: "Make a release",
      blocks: [
        {
          type: "code",
          lang: "powershell",
          text: `tools\\make-release.ps1 -Version 0.1.0              # build and package into C:\\bb\\release
tools\\make-release.ps1 -Version 0.1.0 -Publish     # ... and create a draft GitHub release`,
        },
        {
          type: "p",
          text: "It builds and packages the port and shadNet, adds the launcher's setup and texts, and checks the result: everything a player needs is there, and no game file, save, log or setting is. Build from a clean, pushed commit; check the draft on GitHub, then publish it.",
        },
      ],
    },
    {
      id: "rig",
      title: "The test rig",
      blocks: [
        {
          type: "p",
          text: "`tools/rig` starts two to four low-spec games on one PC, headless, drives them through scenarios (join, lamp trips, boss kills, hits on a shared health bar, party wipes, crashes and rejoins) and checks their logs. Changes run through it alongside the unit and contract tests.",
        },
      ],
    },
    {
      id: "website",
      title: "This website",
      blocks: [
        {
          type: "p",
          text: "`website/` is a Next.js app with shadcn/ui components. `npm ci`, then `npm run dev`; `npm run check` runs the lint, the type check, the tests and the production build.",
        },
      ],
    },
  ],
};

const credits: DocPage = {
  slug: "credits",
  title: "Credits and license",
  description:
    "The projects Bloodborne Seamless Co-op is built on, its GPL license, and its relationship to Sony Interactive Entertainment and FromSoftware (none).",
  sections: [
    {
      id: "credits",
      title: "Built on",
      blocks: [
        {
          type: "p",
          text: "This project stands on the work of many people. Each part keeps its authors' history and license.",
        },
        {
          type: "table",
          head: ["Project", "What it brings"],
          rows: [
            [
              "[shadPS4](https://github.com/shadps4-emu/shadPS4)",
              "the renderer and shader recompiler the port draws with, and the network and NP libraries online play uses",
            ],
            [
              "[Wozzardman/shadp2p](https://github.com/Wozzardman/shadp2p)",
              "shadPS4 with peer-to-peer networking and Bloodborne online play: where this co-op started",
            ],
            [
              "[Wozzardman/shadnet-p2p](https://github.com/Wozzardman/shadnet-p2p)",
              "the shadNet server, by the shadPS4 team, with P2P support",
            ],
            [
              "[deadinside28/bloodborne_pc](https://github.com/deadinside28/bloodborne_pc)",
              "Bloodborne PC, the native port",
            ],
            [
              "[Supermedo/bloodborne_pc](https://github.com/Supermedo/bloodborne_pc)",
              "the port on Windows, the launcher in 13 languages, DLSS, FSR 3.1 and FSR 4",
            ],
            [
              "[AdrianCsT/bloodborne_pc](https://github.com/0xCydral/bloodborne_pc)",
              "the redesigned launcher, Install from PKG, frame generation, XeSS, ReShade, the mouse camera and many fixes",
            ],
            [
              "[The Hunter's Dream](https://thehuntersdream.com) by droogie",
              "the community server for Bloodborne's online content, whose service layout the backend follows",
            ],
            [
              "Kyo, Lance McDonald, auser1337, illusion, emoose and others",
              "the community game patches for 1.09: frame rate, resolution, effects",
            ],
            [
              "[Elden Ring Seamless Co-op](https://www.nexusmods.com/eldenring/mods/510) by LukeYui, Elden Ring Nightreign",
              "the ideas: seamless co-op and party death (no code)",
            ],
          ],
        },
      ],
    },
    {
      id: "license",
      title: "License",
      blocks: [
        {
          type: "p",
          text: `GNU General Public License v2.0 or later, like shadPS4 and the port it is built on ([LICENSE](${blob("LICENSE")})). Third-party components keep their own licenses, listed in the release's \`CREDITS.txt\` and \`licenses\` folder.`,
        },
        {
          type: "note",
          text: "Bloodborne is a trademark of Sony Interactive Entertainment. This project contains no game code or assets and is not affiliated with, endorsed or sponsored by Sony Interactive Entertainment or FromSoftware.",
        },
      ],
    },
  ],
};

export const docs: DocPage[] = [install, architecture, netcode, build, credits];

export function getDoc(slug: string): DocPage | undefined {
  return docs.find((d) => d.slug === slug);
}
