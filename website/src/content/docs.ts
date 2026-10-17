// The documentation, as data. The pages render it, and /llms-full.txt prints the same text as
// Markdown, so people and machines read one source. Inline text understands `code`, **bold** and
// [links](/path).

import type { FigureId } from "@/components/figures";
import { packetRates, rateTotal, roles } from "@/content/traffic";
import { ISSUES_URL, RELEASES_URL, REPO_URL, blob, netcodeStatus } from "@/lib/site";

export type Block =
  | { type: "p"; text: string }
  | { type: "ul"; items: string[] }
  | { type: "ol"; items: string[] }
  | { type: "code"; text: string; lang?: string }
  | { type: "table"; head: string[]; rows: string[][] }
  | { type: "note"; text: string }
  | { type: "h3"; text: string }
  /** Terms and what they mean, like the game's two-column menus: a label, then its text. */
  | { type: "terms"; items: [string, string][] }
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

/** The painted icons in public/art, shown beside a section's title. */
export type IconName = "lantern" | "blades" | "bell" | "tricorn" | "vial" | "skull" | "hand";
export type Section = { id: string; title: string; icon?: IconName; blocks: Block[] };

export type DocPage = {
  slug: string;
  title: string;
  /** One or two sentences: the page's meta description and its line in /llms.txt. */
  description: string;
  sections: Section[];
};

const install: DocPage = {
  slug: "install",
  title: "Install and set up",
  description:
    "What you need, how to install Bloodborne Seamless Co-op, what each part of the launcher's Co-op page is for, and how the other players reach the host.",
  sections: [
    {
      id: "requirements",
      title: "Before you start",
      blocks: [
        {
          type: "ul",
          items: [
            "Your own Bloodborne for PS4 at version 1.09: the game folder with the 1.09 update, or the PS4 `.pkg` files. Nothing from the game is included.",
            "Windows 10 (1903 or later) or Windows 11, 64-bit.",
            "A graphics card with Vulkan 1.3 and a current driver.",
            "Everyone, the host included, on the same release. The server turns away a different build.",
          ],
        },
        { type: "note", text: netcodeStatus.note },
      ],
    },
    {
      id: "install",
      title: "Install",
      blocks: [
        {
          type: "ol",
          items: [
            `Download the setup \`.exe\` from the [download page](/download) or [GitHub releases](${RELEASES_URL}) and run it. It installs for your Windows user and adds a desktop shortcut.`,
            "Open **Bloodborne Seamless Co-op**. The launcher opens on **Home**, with the latest news.",
            "Go to **Co-op** in the sidebar and fill it in (below). **Done** lights up once nothing is missing. Then press **PLAY**.",
          ],
        },
        {
          type: "image",
          src: "/docs/launcher-home.webp",
          width: 1200,
          height: 780,
          alt: "The launcher's Home page: the news card Hunt Yharnam together, the sidebar with Home, Co-op and Settings, and the bottom bar with the status icons, Start server, Stop server and PLAY.",
          caption: "Home: the news, the sidebar and the PLAY bar.",
        },
      ],
    },
    {
      id: "coop-card",
      title: "The Co-op page",
      blocks: [
        {
          type: "p",
          text: "One page for everyone, host or not. You fill it in once.",
        },
        {
          type: "image",
          src: "/docs/launcher-coop.webp",
          width: 1200,
          height: 696,
          alt: "The Co-op page filled in: the Bloodborne folder with a green check for version 01.09, the host's IP 10.0.0.2, Enemies Off, the Bring saves button and Done.",
          caption: "Filled in: the folder is found, the host's IP is set and Enemies is Off.",
        },
        {
          type: "terms",
          items: [
            [
              "Your Bloodborne folder",
              "The game, at 1.09 or with the 1.09 update next to it. **Browse…** picks the folder; **Find it for me** searches the usual places and then every drive; **Install from PKG…** installs the game and the update from your `.pkg` files. The line under it says which version it found, or what is missing.",
            ],
            [
              "Host's IP address",
              "Where the host's PC can be reached: its VPN address, or its public IP. The host types their own address too; that is how the launcher knows this PC is the host.",
            ],
            [
              "Enemies",
              "Off is the normal game. **Harder** gives every enemy and boss three times the HP and poise and half again the damage, to make up for the extra hunters. Everyone picks the same.",
            ],
            [
              "Bring saves from another folder…",
              "Opens a folder picker and copies your characters over from a PS4 save folder, a shadPS4 `user` folder or an older co-op install.",
            ],
            [
              "Done",
              "Greyed out until the page is complete. The line beside it says what is still needed.",
            ],
          ],
        },
        {
          type: "image",
          src: "/docs/launcher-coop-empty.webp",
          width: 1200,
          height: 696,
          alt: "The Co-op page on first start: the folder, the host's IP and Enemies empty, Done greyed out and the line Still needed: the game folder, the host's IP, the enemies.",
          caption:
            "On first start, Done waits until the folder, the host's IP and the enemies are set.",
        },
        {
          type: "p",
          text: "There is no account to make. The others see your character's name.",
        },
      ],
    },
    {
      id: "play",
      title: "Press PLAY",
      blocks: [
        {
          type: "ol",
          items: [
            "When the host's IP is this PC's own address, PLAY starts the co-op server first. If it ever stops, it restarts by itself.",
            "The first time, Windows asks once to let co-op through its firewall. Say yes: the launcher adds the rules for you.",
            "The first start prepares a game folder that links to your game. Nothing is copied.",
            "The game starts. Making a team happens in the game: see [Playing together](/docs/play).",
          ],
        },
        {
          type: "image",
          src: "/docs/launcher-bar.webp",
          width: 1200,
          height: 97,
          alt: "The launcher's bottom bar: four status icons, Start server, Stop server and PLAY, with the frame rate, upscaler and resolution under the icons.",
          caption: "The bottom bar.",
        },
        {
          type: "image",
          src: "/docs/launcher-server-running.webp",
          width: 1200,
          height: 97,
          alt: "The same bottom bar on the host with the server shown as Running.",
          caption: "On the host, with the server running.",
        },
        {
          type: "terms",
          items: [
            [
              "The four icons",
              "Quick checks of your setup. A green check means ready, a grey dot means it is not set yet.",
            ],
            [
              "Start server, Stop server",
              "For running the server without the game, for example when the host plays later. The word beside them says whether it is running.",
            ],
            [
              "The line under them",
              "Your frame rate, upscaler, resolution and mode, or the last thing the launcher did.",
            ],
          ],
        },
        {
          type: "p",
          text: "If the game crashes, the next PLAY sends the crash report and the logs to the host's server, so the host can pass them on.",
        },
      ],
    },
    {
      id: "network",
      title: "Reach the host",
      blocks: [
        {
          type: "p",
          text: "The host's PC is the server, so every other player has to reach it. Either:",
        },
        {
          type: "ul",
          items: [
            "join the same VPN, such as Radmin VPN, Tailscale or ZeroTier, and use the host's VPN address (the easy way), or",
            "forward these ports on the host's router and use its public IP: TCP **31313**, **31315** and **20443**, and UDP **31314**.",
          ],
        },
        {
          type: "p",
          text: "All co-op traffic goes through UDP 31314. The games never talk to each other directly.",
        },
      ],
    },
    {
      id: "graphics",
      title: "Graphics and frame rate",
      blocks: [
        {
          type: "p",
          text: "**Settings** holds the port's own options. **General** has the ones most players change: output resolution, upscaler and preset, frame generation, languages and window mode. The tabs above it go further: graphics, display and FPS, controls, mods and the log. Co-op runs at a fixed 60 FPS: the game sends its updates once per frame, and every player has to send at the same pace.",
        },
        {
          type: "image",
          src: "/docs/launcher-settings.webp",
          width: 1200,
          height: 780,
          alt: "Settings, General: Output 1920 by 1080, Frame rate 60 FPS, Upscaler FSR 4, Preset Quality, Game and launcher language, Windowed, Frame generation, and Saves and shortcuts.",
          caption: "Settings, General.",
        },
      ],
    },
    {
      id: "problems",
      title: "When something goes wrong",
      icon: "skull",
      blocks: [
        {
          type: "terms",
          items: [
            [
              "✗ co-op needs 1.09",
              "The launcher found the game without the update. Put the update folder next to the game, or install both from the `.pkg` files.",
            ],
            [
              "Co-op could not start",
              "Usually the folder or the host's IP is empty or wrong. On the host, check that nothing else uses the ports above.",
            ],
            [
              "Nobody can join",
              "The others can't reach the host. Check that everyone is on the same VPN, or that the ports are forwarded, and that the host allowed the firewall prompt.",
            ],
            [
              "A crash",
              `Report it on [GitHub issues](${ISSUES_URL}) with the crash report and say which release every player was on.`,
            ],
          ],
        },
      ],
    },
  ],
};

const play: DocPage = {
  slug: "play",
  title: "Playing together",
  description:
    "How to make a team in the game, what the party list shows, how going down and reviving work, and how to give items to a teammate.",
  sections: [
    {
      id: "team",
      title: "Make a team",
      icon: "bell",
      blocks: [
        {
          type: "p",
          text: "**Continue**, **Load Game** or **New Game** on the title screen opens the team screen, The Hunt Together. In the game, press **Insert** (or **L3 + R3**) and choose **Co-op Team**.",
        },
        {
          type: "image",
          src: "/docs/team-host.webp",
          alt: "The Hunt Together team screen: four places, Player 1 as leader choosing a character, Player 2 ready, two empty places, the Hunters online and Invitations lists, and the buttons Leave the team, Begin the hunt and Play alone.",
          caption: "The leader's view. Each place shows a hunter and where they are.",
          width: 1440,
          height: 810,
        },
        {
          type: "terms",
          items: [
            [
              "Create a team",
              "Makes you the leader. The leader's game hosts the hunt; the server keeps the world.",
            ],
            [
              "Hunters online",
              "Everyone signed in to your server. **Invite** calls one of them to your team.",
            ],
            ["Invitations", "Who called you. **Join** or **Decline**."],
            ["Ready", "You have picked your character. The hunt waits for every hunter."],
            ["Begin the hunt", "Leader only, once everyone is ready."],
            ["Play alone", "Your character plays its own world, without the team."],
            ["Leave the team", "Back to no team. The leader can also **Remove** a member."],
          ],
        },
        {
          type: "image",
          src: "/docs/team-guest.webp",
          alt: "The same team screen seen by Player 2, with the Ready button instead of Begin the hunt.",
          caption: "A member's view: Ready instead of Begin the hunt.",
          width: 1440,
          height: 810,
        },
        {
          type: "p",
          text: "After **Begin the hunt** every game loads its own save. The loading screen, The Hunt Begins, lists each hunter as Loading, Joining the hunt or Ready, and opens for everyone at once. You arrive where your character stood. Starting the game again after a crash puts you back in the team.",
        },
        {
          type: "image",
          src: "/docs/loading.webp",
          alt: "The Hunt Begins loading screen over a night city under the moon, with cards for Player 1 and Player 2, both Joining the hunt at about 80 percent.",
          caption: "The Hunt Begins: everyone loads, then the hunt opens at once.",
          width: 1440,
          height: 810,
        },
        {
          type: "image",
          src: "/docs/insert-team.webp",
          alt: "The Co-op Team page opened from the Insert menu during play: Player 1 and Player 2 In the hunt, two empty places, Hunters online and Invitations.",
          caption: "Co-op Team from the Insert menu, while you play.",
          width: 1440,
          height: 810,
        },
      ],
    },
    {
      id: "world",
      title: "One world",
      icon: "lantern",
      blocks: [
        {
          type: "ul",
          items: [
            "Enemies, their health and their deaths are shared. Everyone's hits land on the same health bar.",
            "Doors, levers, shortcuts, lamps and beaten bosses belong to the team's world, which the server keeps on the host's disk.",
            "Items you pick up, Blood Echoes and Insight are your own. NPC gifts are per player, so everyone gets their own.",
            "Everyone travels on their own. Nobody is pulled to the host or sent home.",
          ],
        },
      ],
    },
    {
      id: "party",
      title: "The party list",
      icon: "tricorn",
      blocks: [
        {
          type: "p",
          text: "Your teammates are listed on the left, under your own bars: a portrait, the name and the health bar. It reads **Down** with the revive progress, **Dead**, or **elsewhere** when they are on another map.",
        },
        {
          type: "image",
          src: "/docs/party-alive.webp",
          alt: "A teammate in the party list: a round framed portrait, the name Player 2 and a health bar.",
          caption: "A teammate on your map.",
          width: 1040,
          height: 340,
        },
      ],
    },
    {
      id: "down",
      title: "Going down",
      icon: "hand",
      blocks: [
        {
          type: "p",
          text: "While a teammate stands on your map, a killing blow puts you down instead of killing you. The screen edge turns red and reads **You are down**.",
        },
        {
          type: "image",
          src: "/docs/party-downed.webp",
          alt: "The same teammate shown as downed: an empty health bar and a red blood-drop badge on the portrait.",
          caption: "How the others see you while you are down.",
          width: 1040,
          height: 340,
        },
        {
          type: "terms",
          items: [
            [
              "Reviving",
              "Strike the downed hunter. The prompt shows **strike to revive** and how far along it is. The first down needs as much damage as their full health, the second twice that, then four times. They get up with half their health and two seconds of safety.",
            ],
            ["Giving up", "Hold the dodge button for three seconds to die at once."],
            [
              "A wipe",
              "When everyone on a map is down, the party dies and wakes at its lamps. The map resets as it would after a death, but the team's kills stay.",
            ],
          ],
        },
      ],
    },
    {
      id: "give",
      title: "Giving items",
      icon: "vial",
      blocks: [
        {
          type: "p",
          text: "In the inventory, an item's menu lists **Give to** for each teammate nearby, and **Give Blood Echoes to** for echoes. Pick how many; the teammate accepts in the game's own dialog. Weapons go one at a time.",
        },
        {
          type: "image",
          src: "/docs/give-menu.webp",
          alt: "The inventory with an item's menu open: Use, Discard, Give and Give Blood Echoes.",
          caption: "Giving from the inventory.",
          width: 1280,
          height: 720,
        },
        {
          type: "image",
          src: "/docs/give-items.webp",
          alt: "In game, a guest receives four Blood Vials from the host, shown in the game's own item dialog, with the host's name and health above their head.",
          caption: "Four Blood Vials arriving from the host.",
          width: 1280,
          height: 720,
        },
      ],
    },
    {
      id: "settings",
      title: "Settings in the game",
      blocks: [
        {
          type: "p",
          text: "**Insert** on the keyboard, or **L3 + R3** on a pad, opens the port's settings over the game, with Co-op Team, Apply and restart the game and Quit to desktop. The frame rate shows 60 (co-op) and can't be changed while playing together.",
        },
        {
          type: "image",
          src: "/docs/insert-menu.webp",
          alt: "The Insert settings menu over the game, on the Display tab, with Co-op Team selected above Quit to desktop.",
          caption: "The Insert menu, with Co-op Team at the bottom.",
          width: 1220,
          height: 830,
        },
      ],
    },
  ],
};

const architecture: DocPage = {
  slug: "architecture",
  title: "How it works",
  description:
    "The parts of Bloodborne Seamless Co-op: the native PC port, the co-op code built into it, the patched map events and the server one player runs.",
  sections: [
    {
      id: "overview",
      title: "Overview",
      blocks: [
        {
          type: "figure",
          figure: "architecture",
          caption: "Every game keeps one link to the co-op server on the host's PC.",
          alt: "Up to four games (Bloodborne PC with the co-op hooks) each connect over UDP to one co-op server, which holds accounts, presence and the team lobby, the session with one enemy owner per map, the world log saved to disk, and the relay of game packets. The games never connect to each other.",
        },
        {
          type: "p",
          text: "Two programs: the game, which everyone runs, and the server, which the host runs next to their game.",
        },
      ],
    },
    {
      id: "port",
      title: "The port",
      blocks: [
        {
          type: "p",
          text: "Bloodborne PC loads the PS4 game's own x86-64 code and runs it on a small runtime, with shadPS4's renderer drawing through Vulkan. There is no emulator in between, so hooks, memory, threads, files and networking are direct. It keeps what the port offers (high frame rates, 4K, DLSS, FSR, XeSS, frame generation, HDR, ReShade, mouse and keyboard) and builds the co-op code in.",
        },
      ],
    },
    {
      id: "coop-code",
      title: "The co-op code",
      blocks: [
        {
          type: "p",
          text: "The co-op code changes the 1.09 game at about 125 known places, each listed in one table (`emulator/src/core/bloodborne_patch_sites.h`) with the bytes it expects. The launcher warns when another game patch writes over one of them.",
        },
        {
          type: "p",
          text: "It opens the game's session from the team instead of a summoning bell, removes the things that would end a normal visit (boss kills, lamp trips, deaths, map changes, area limits), shares enemies and world state, and adds party death, giving items and the harder enemies option.",
        },
      ],
    },
    {
      id: "events",
      title: "Map events",
      blocks: [
        {
          type: "p",
          text: "Some of the game's event scripts stop early for a summoned player: boss rewards, boss fog, the first visit to the Hunter's Dream. Setup patches copies of your own event files (`tools/re/party_boss_events.py` checks that only the intended instructions changed). The release carries only their expected hashes, never game files.",
        },
      ],
    },
    {
      id: "server",
      title: "The server",
      blocks: [
        {
          type: "p",
          text: "`server/` is a shadNet server with the co-op server added (`server/coop`: the protobuf messages, the world log, the team lobby, trading). shadNet also answers the game's own online requests, so the game goes online without PlayStation Network or any public server. How the server and the games talk is on [the server netcode](/docs/netcode) page.",
        },
      ],
    },
    {
      id: "layout",
      title: "Repository",
      blocks: [
        {
          type: "terms",
          items: [
            [
              "bbport/",
              "Bloodborne PC, the native port, with the launcher and the Windows packaging",
            ],
            [
              "emulator/",
              "shadPS4 with networking; `src/core/bloodborne_*` is the co-op code the port builds in",
            ],
            ["server/", "shadNet and the co-op server"],
            ["backend/", "a stand-in for the game's online services"],
            [
              "tools/",
              "build scripts, local checks, releases, the test rig, reverse-engineering helpers",
            ],
            ["docs/", "design notes"],
            ["website/", "this site"],
          ],
        },
        {
          type: "p",
          text: `Each upstream project is imported with its history, so \`git log\` shows its authors. The design notes go deeper: [BBPORT.md](${blob("docs/BBPORT.md")}), [WORLD_SYNC.md](${blob("docs/WORLD_SYNC.md")}) and [FLAGS.md](${blob("docs/FLAGS.md")}).`,
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
            "Every game packet goes through one send and one receive point. Packets travel on 68 numbered channels (about 60 in use), some unreliable (movement) and some reliable (everything that must arrive). Hooking those two points lets the co-op code see, drop, rewrite or inject any packet.",
            "The native model is one host's world. The host runs every enemy and sends their positions and HP; a guest's hit on an enemy goes to the host, which applies it. A joining guest gets the host's flags for the host's current map only, once.",
            "Players sync themselves. Each game sends its own player: position, heading, HP and the animation state. A hit on a player goes to the victim's game, which decides its HP.",
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
            "One UDP socket per game to the server, on a fixed port next to the TCP one. Each datagram holds messages; each message has a one-byte kind and a length.",
            "Kinds: `Game` (a small fixed header `{channel, reliable bit, sender member, map, visit}` followed by the game's packet untouched), `Proto` (one protobuf message), `Ack` and `Ping`.",
            "Our own reliability layer, per member and per class: sequence numbers, acks, resends and in-order delivery for reliable traffic (the game's reliable channels, world-log entries, snapshots). Unreliable traffic such as movement is never resent; the newest wins. Who is connected, outages and timeouts come from this layer, not from the game's own timers.",
            "Protobuf messages (`server/proto/coop.proto`, shared by the game and the server): `Hello` (account, build, protocol), the lobby (`TeamCreate`, `Invite`, `InviteAnswer`, `TeamState`, `Ready`, `Start`), the session (`Place`, `MemberJoined`, `MemberLeft`, `EnemyOwner`), the world log (`Propose`, `Entries`, `SnapshotRequest`, `Snapshot`), vitals, party death and portraits.",
            "One build for everyone. `Hello` carries the build, and a different build is refused at the lobby with a clear message. There are no compatibility fallbacks.",
          ],
        },
      ],
    },
    {
      id: "traffic",
      title: "What a game sends",
      blocks: [
        {
          type: "p",
          text: "Measured on the test rig: two players at 30 FPS on one PC, from the server's per-kind counters and the games' own logs. The game sends each packet once per teammate, and the server forwards it only to teammates on the same map, so a player on another map gets almost nothing from the others.",
        },
        {
          type: "figure",
          figure: "rates",
          caption:
            "Game packets a game hands to each teammate per second, by its role. The follower's total is measured; its split is partly inferred.",
          alt: "Grouped bar chart; the same numbers are in the table below.",
        },
        {
          type: "table",
          head: ["Kind", ...roles.map((r) => r.name)],
          rows: [
            ...packetRates.map((r) => [
              r.kind,
              String(r.owner),
              String(r.follower),
              String(r.lone),
            ]),
            ["**Total**", ...roles.map((r) => `**~${rateTotal(r.key)}**`)],
          ],
        },
        {
          type: "ul",
          items: [
            "Event sync is most of the owner's packets. It is reliable, so every packet is acknowledged. It ran at about 2 a second in an earlier trace, so the current rate is being looked into.",
            "Frame rate sets most of the rates. Enemy HP goes every 4th frame and player state every 5th, so at the 60 FPS used online both double.",
            "Bandwidth is not the limit. Under 10 KB/s per game with two players; with three, the host's upload peaks around 13 KB/s. The rig link had 2 ms round trips and no resends.",
          ],
        },
        {
          type: "figure",
          figure: "upload",
          caption:
            "Bytes each game sent to the server per second, from the server's 10-second link counters. While the guest was on another map it ran that map's enemies, so its upload rose; the server forwarded none of the host's same-map packets to it.",
          alt: "Line chart over 15 minutes: the host uploads about 6 to 9 KB/s, the guest about 2 to 4 KB/s. While the guest was on another map, from minute 11 to 13.5, the guest's upload rose to about 4.5 KB/s and the host's fell slightly.",
        },
        { type: "h3", text: "Where the server sends each kind" },
        {
          type: "table",
          head: ["Kind", "Route"],
          rows: [
            [
              "Player state, event sync, enemy AI records, forced animations, script calls",
              "teammates on the same map",
            ],
            [
              "Enemy positions, HP and catch-up for joiners",
              "from the map's enemy owner to the others on that map",
            ],
            ["A teammate's hits on enemies, enemy near lists", "to the map's enemy owner only"],
            ["Player hits, throws, bullets, objects", "teammates on the same map"],
            ["Join handshake, character data", "every teammate"],
            ["Map items, door corrections, the boss-room kick", "never forwarded"],
          ],
        },
        { type: "h3", text: "Our own messages" },
        {
          type: "p",
          text: "Besides the game's packets, each game and the server exchange protobuf messages on their own reliable stream: about five a second each way when nothing changes.",
        },
        {
          type: "table",
          head: ["Message", "Direction", "How often"],
          rows: [
            ["Ping", "game and server", "once a second"],
            [
              "Map and enemy owners board",
              "game to server to team",
              "once a second, and at once on a map change",
            ],
            [
              "Vitals (HP, downed)",
              "game to server to team",
              "once a second; 4 a second while someone is down",
            ],
            ["HP proposals", "game to server", "twice a second, and at once on a change"],
            ["Enemy hits", "game to server to team", "one per hit that takes HP"],
            ["World-log entries", "server to team", "heartbeat once a second, at once on news"],
            [
              "Flags (doors, lamps, levers)",
              "game to server",
              "at once, then every half second for 10 seconds",
            ],
            ["Map state report", "host to server", "about every 6 seconds, ~1.25 KB"],
            ["Portrait", "game to server to team", "every 15 seconds, 16 KB"],
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
            "The server stores the worlds itself, with atomic writes and backups. Every accepted world-log entry is written at once, so a crash, a quit or a host change loses nothing, and any member can be the game-level host.",
            "A new team world starts from the leader's progress.",
            "Every save keeps its personal world. Loading into a team world puts the save's own world aside and applies the team's; playing solo again brings it back. The character is never touched by this.",
            "Bosses beaten solo stay in that save's personal world; in a team only the team world counts.",
            "Up to four hunters per team: the game's remote player slots.",
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
          text: "Continue, Load Game or New Game opens the team screen, where the server keeps the team: its members, the hunters online and the invitations. When the leader begins the hunt, every member's game loads its own save; the first to load becomes the game-level host (the server still owns the world) and the others join its session as they finish loading. After a crash or a quit, starting the game again rejoins the team. Using it is on [Playing together](/docs/play).",
        },
        {
          type: "figure",
          figure: "join-flow",
          caption: "From the title screen to one world, with no bell and no signs.",
          alt: "Six steps: Continue, Load Game or New Game; the team screen, to create a team or accept an invite; everyone picks a character and marks Ready; the leader begins the hunt; loading holds until the team is together; one world, each hunter where they stand.",
        },
      ],
    },

    {
      id: "status",
      title: "Status",
      blocks: [
        {
          type: "p",
          text: `The rewrite was built in phases on a branch, each with tests first and runs on the multi-game rig: a packet trace, a channel policy table, one UDP link to the server, the world log on the server, enemy owners from the server, the native team lobby, the server owning the session (faster joins, host migration, resuming after a drop), one owner per map with a single HP path, server routing with the old peer-to-peer paths deleted, and crash-safe world saves with a server that restarts without ending sessions. It is in main, in testing, and ships in the next public release.`,
        },
        {
          type: "p",
          text: `The research and the plan with every phase are in the repository's design notes: [NATIVE_NETCODE.md](${blob("docs/NATIVE_NETCODE.md")}) and [SERVER_COOP_PLAN.md](${blob("docs/SERVER_COOP_PLAN.md")}).`,
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
            "The port: [MSYS2](https://www.msys2.org) with the CLANG64 packages listed in `bbport/packaging/windows/README.md`, and a python.org Python 3.10 or later (it freezes the launcher).",
            "The server and emulator: VS 2022 Build Tools (C++ workload, Windows SDK), LLVM 20.1.8 (clang-cl), CMake, Ninja, Qt 6.8.3 `msvc2022_64`.",
            "Checks: Python 3.12 with `clang-format==19.1.7` and `reuse`, and [jj](https://jj-vcs.github.io/jj/) for the local CI script.",
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

export const docs: DocPage[] = [install, play, architecture, netcode, build, credits];

export function getDoc(slug: string): DocPage | undefined {
  return docs.find((d) => d.slug === slug);
}
