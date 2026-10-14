# Bloodborne Seamless Co-op

Seamless co-op for **Bloodborne** on PC, in the spirit of
[Elden Ring Seamless Co-op](https://www.nexusmods.com/eldenring/mods/510): up to four hunters
make a team, share one world, join each other without bells, signs or Insight, and keep playing together through
lamp trips, deaths and boss fights.

It runs on **Bloodborne PC**, the native Windows port of the PS4 game (the game's own code runs on
your CPU, its graphics go through Vulkan; there is no emulator in between). The co-op server and
everything else it needs run on the players' own PCs.

> [!IMPORTANT]
> **No game files are included.** You need your own copy of Bloodborne for PS4, version **1.09**:
> a decrypted dump of the game folder with the 1.09 update, or the PS4 `.pkg` files (the included
> launcher installs them). This project is not affiliated with Sony Interactive Entertainment or
> FromSoftware.

**Status: experimental, playable.** It is played regularly in parties of two and three and
checked by an automated two- and three-game test rig on every change, but it is a fan project:
expect rough edges and keep backups of your saves.

## Contents

1. [What it does](#what-it-does)
2. [Play it](#play-it)
3. [How it works](#how-it-works)
4. [Repository layout](#repository-layout)
5. [Build from source](#build-from-source)
6. [Make a release](#make-a-release)
7. [Credits and forks](#credits-and-forks)
8. [License](#license)

## What it does

- **Teams, no bells.** Continue, Load Game or New Game opens the team screen: make a team of up to
  four from the hunters online, Ready, and the leader begins the hunt. Every game loads its own
  save and the team is in one world a few seconds later, wherever each of you stands: no bells,
  no signs, no summoning.
- **The team's world.** The team keeps one world on the server, started from the leader's save;
  each save's own world is kept aside and comes back when it plays alone.
- **Stay together.** Nobody is sent home: lamp trips, the Hunter's Dream, deaths, boss kills and
  map changes all keep the party together. A guest who crashes or quits comes back into the same
  session, where they were.
- **One world.** Enemies, their HP and their deaths are shared: every player's hits count on the
  same health bar, and a kill is a kill for everyone. NPCs, doors, lamps, shortcuts and boss kills
  follow the host's world, and what a guest unlocks is kept in their own world too. Each player
  keeps their own pickups, echoes and Insight.
- **Party death** (from Elden Ring Nightreign, always on): a killing blow puts you down instead of
  killing you while a partner stands on your map. They revive you by striking you; when everyone
  on a map is down, the party wipes and wakes at its lamps.
- **No stolen bloodstains.** Your echoes stay where you died until you pick them up.
- **Harder enemies** (optional): HP, damage and poise multipliers for every enemy and boss, to make
  up for the extra hunters.
- **Self-hosted.** One player runs a small server (accounts, the team lobby, the teams' worlds,
  every game's co-op traffic) and a stand-in for the game's online services. No PlayStation Network, no community server, nothing in the cloud.
- **Everything from Bloodborne PC**: 60 FPS and above, resolutions up to 4K, DLSS, FSR 3.1/4 and XeSS
  upscaling, frame generation, HDR, ReShade, mouse and keyboard, gamepads.

## Play it

1. Download the setup `.exe` from the
   [releases page](https://github.com/lKaniel/bloodborne-server/releases) and run it. Every player
   in a team uses the same release.
2. Open **Bloodborne Seamless Co-op** (the launcher). It finds your game (or **Install from PKG**
   extracts it); on the Co-op card choose host (runs the co-op server on this PC) or player, the
   host's IP and an account.
3. Press **PLAY**. The host's server starts by itself; Windows asks once to let co-op through its
   firewall.
4. Continue, Load Game or New Game opens the team screen: create a team or accept an invite, Ready,
   and the leader presses **Begin the hunt**.
5. Everyone must reach the host's PC: the same VPN (Radmin VPN, Tailscale, ZeroTier) or the host's
   public IP with ports forwarded: TCP 31313, 31315, 20443 and **UDP 31314**. The UDP port carries
   every game's co-op traffic (the games never talk to each other directly), so it must be open.

Requirements: Windows 10 (1903 or later) or 11, 64-bit, and a Vulkan 1.3 graphics card with a current
driver. Python, the libraries and the servers are all inside the zip. Tested with CUSA03173 (the
European Game of the Year edition) at 1.09; all players in a party should use the same edition.

## How it works

```text
 Player's PC (each player)                          Host's PC (also plays)
 ┌──────────────────────────────────────────┐       ┌────────────────────────────────────┐
 │ Bloodborne PC (bbport): the 1.09 eboot   │ TCP   │ shadnet (server/): accounts, rooms │
 │ runs natively, renderer from shadPS4     │◄─────►│ and signaling                      │
 │  + co-op code (emulator/src/core/        │ UDP   │  + the co-op server: the team      │
 │    bloodborne_*): hooks and patches in   │◄─────►│    lobby, the teams' worlds, enemy │
 │    the game, team join, world sync       │ 31314 │    owners, and the relay of every  │
 │  + co-op map events (made by setup from  │       │    game's packets                  │
 │    the player's own event files)         │ HTTP  ├────────────────────────────────────┤
 │                                          │◄─────►│ the game's own online services     │
 └──────────────────────────────────────────┘       └────────────────────────────────────┘
   every game talks only to the server: one UDP link each, no peer-to-peer
```

- **The game runs itself.** Bloodborne PC loads the PS4 game's own x86-64 code and runs it on a
  small runtime with shadPS4's renderer. The co-op code is compiled into it (`bbport/gpu/bbcoop`
  wires it in) and changes the game at known places in the 1.09 eboot: about 125 patch sites and
  hooks, listed in one table (`emulator/src/core/bloodborne_patch_sites.h`); the launcher warns
  when another game patch writes over one of them.
- **Joining uses the game's own multiplayer, without summoning.** The team's host opens its game
  session itself (the game's own call) and sends its join data to the server; each member's game
  joins that session directly as soon as it stands in its world. Both games then run a normal
  Bloodborne session; the co-op code removes the limits that would end it (boss kills, lamp
  trips, deaths, map changes, the area restrictions) and puts players back together after loads.
- **The server in the middle.** Every game packet and co-op message goes through the server over
  one UDP link with its own resend and ordering (`docs/SERVER_COOP_PLAN.md`). The server keeps the
  team's world (kills, event flags, map state, enemy HP) as an ordered log, names one owner per
  map who runs its enemies, and keeps the teams and their worlds on disk across restarts. Who owns
  each event flag (host, per player, shared) is one table (`docs/FLAGS.md`).
- **Map events.** Some of the game's event scripts end early for a summoned player (boss rewards,
  boss fog, the first visit to the Dream). Setup patches copies of the player's own event files
  (`tools/re/party_boss_events.py`, which checks that only the intended instructions changed);
  the release only carries their expected hashes.
- **Self-hosted services.** `server/` is a shadNet server with the co-op server added (`server/coop`:
  the protobuf messages, the world log, the team lobby, trading). The game's own online requests (JSON over HTTP, as the PS4 game sends them)
  are answered by shadnet itself (`BloodborneBackend`, on by default) or the Python `backend/`, so
  the game goes online without the official or community servers.

More detail: [docs/BBPORT.md](docs/BBPORT.md) (the port and its co-op wiring),
[docs/SEAMLESS_COOP_PLAN.md](docs/SEAMLESS_COOP_PLAN.md) (the original plan),
[docs/WORLD_SYNC.md](docs/WORLD_SYNC.md), [docs/FLAGS.md](docs/FLAGS.md), and the handover notes
[docs/HANDOFF-PORT.md](docs/HANDOFF-PORT.md) and [docs/HANDOFF.md](docs/HANDOFF.md).

## Repository layout

| Folder | What it is | Upstream |
|---|---|---|
| `bbport/` | Bloodborne PC, the native port, with the launcher and the Windows packaging | [deadinside28/bloodborne_pc](https://github.com/deadinside28/bloodborne_pc), [Supermedo/bloodborne_pc](https://github.com/Supermedo/bloodborne_pc), [AdrianCsT/bloodborne_pc](https://github.com/0xCydral/bloodborne_pc) |
| `emulator/` | shadPS4 with P2P networking; `src/core/bloodborne_*` is the co-op code the port compiles in | [Wozzardman/shadp2p](https://github.com/Wozzardman/shadp2p) (from [shadPS4](https://github.com/shadps4-emu/shadPS4)) |
| `server/` | shadNet server: accounts, rooms, signaling, and the co-op server (team lobby, team worlds, the relay) | [Wozzardman/shadnet-p2p](https://github.com/Wozzardman/shadnet-p2p) |
| `backend/` | stand-in for the game's online services | the LocalServer of the Bloodborne PC Online community package |
| `tools/` | build, local CI, the co-op kits and releases, the test rig (`tools/rig`), RE helpers (`tools/re`) | |
| `docs/` | design notes and handovers | |

All upstreams are imported with their full history (subtree merges), so `git log` shows their
authors. The shadPS4 build of the co-op (`emulator/`) still works but the port is the main target;
port and shadPS4 players cannot be in one party.

## Build from source

Windows only for now. Prerequisites:

- the port: [MSYS2](https://www.msys2.org) with the CLANG64 packages listed in
  [bbport/packaging/windows/README.md](bbport/packaging/windows/README.md), and a python.org
  Python 3.10+ (it freezes the launcher);
- the server and emulator: VS 2022 Build Tools (C++ workload, Windows SDK), LLVM **20.1.8**
  (clang-cl; 23 breaks googletest), CMake, Ninja, Qt 6.8.3 `msvc2022_64` in `C:\Qt` (via `aqt`);
- checks: Python 3.12 with `clang-format==19.1.7` and `reuse`; [jj](https://jj-vcs.github.io/jj/)
  for the local CI script.

```powershell
git clone https://github.com/lKaniel/bloodborne-server.git C:\bb\bloodborne-server
cd C:\bb\bloodborne-server
git config submodule.emulator/externals/mesa-kosmickrisp.update none   # macOS-only
git submodule update --init --recursive
```

`BB_ROOT` (default `C:\bb`) holds the checkout. Build output goes to `C:\bb\out`, outside the tree.

| Script | What it does |
|---|---|
| `tools/build-bbport.ps1 [-Test]` | builds the port (`bbport/build.sh` in MSYS2 CLANG64) and, with `-Test`, its co-op tests |
| `tools/env.ps1` | dot-source it for an MSVC x64 dev shell with LLVM and Qt on PATH |
| `tools/build-emu.ps1` | builds the emulator (`out\emulator\build`) and its unit tests (`out\emulator\tests`) |
| `tools/build-server.ps1` | builds shadnet and its tests (`out\server`) |
| `tools/ci-check.ps1 -Component emulator\|server\|bbport\|backend\|tools [-All]` | local CI: clang-format 19, trailing whitespace, REUSE, build, tests |
| `tools/make-release.ps1 -Version x.y.z` | builds a release zip (see below) |
| `tools/make-port-kit.ps1` | the developers' test kit for a Bloodborne.PC.Online install |
| `tools/rig/run_scenario.py` | the automated multi-game test rig (two or three games on one PC) |

To play a build of your own, run `bash build.sh` and `bash packaging/windows/package.sh` in
`bbport/` (MSYS2 CLANG64 shell) and use `tools/make-release.ps1 -SkipPortBuild -AllowDirty`.

## Make a release

```powershell
tools\make-release.ps1 -Version 0.1.0              # build and zip into C:\bb\release
tools\make-release.ps1 -Version 0.1.0 -Publish     # ... and create a draft GitHub release
```

It builds the port and packages it, builds shadnet with its Qt DLLs and the account tool, adds the
kit's scripts, the release's setup and texts (`tools/release/kit`), the backend and the event
patcher, checks the result (`tools/release/check_release.py`: everything a player needs is there,
and no game file, save, log or setting is), zips it with its SHA-256 and, with `-Publish`, creates
a **draft** release `v<version>` at the build's commit with the zip attached (`gh`, or the GitHub
API with `GITHUB_TOKEN` or git's stored credential). Check the draft on GitHub and press Publish.

- Build from a clean, pushed commit: the script refuses uncommitted changes unless `-AllowDirty`
  (test builds only; those cannot be published).
- `-GameDir <game folder with the 1.09 update>` names the game whose event files the release is
  tested with (default: the rig's game, `C:\bb\rig\port\game\CUSA03173`). Only their hashes go into
  the release (`event-reference.json`); each player's setup compares its own files against them.
- `-DlssSdk <checkout of github.com/NVIDIA/DLSS>` builds the DLSS bridge; without it the release
  has DLSS only if `bbport\out` already holds `bbport_dlss.dll`.
- `-SkipPortBuild` / `-SkipServerBuild` reuse the builds that are there.

## Credits and forks

This project stands on the work of many people. Each part keeps its authors' history and license.

| Project | What it brings |
|---|---|
| [shadPS4](https://github.com/shadps4-emu/shadPS4) | The PS4 emulator: the renderer and shader recompiler the port draws with, and the network and NP libraries the online play uses |
| [Wozzardman/shadp2p](https://github.com/Wozzardman/shadp2p) | shadPS4 with peer-to-peer networking and Bloodborne online play: where this co-op started (`emulator/`) |
| [Wozzardman/shadnet-p2p](https://github.com/Wozzardman/shadnet-p2p) | The shadNet server (by the shadPS4 team) with P2P support (`server/`) |
| [deadinside28/bloodborne_pc](https://github.com/deadinside28/bloodborne_pc) | Bloodborne PC, the native port (releases 0.4 to 0.51 merged) |
| [Supermedo/bloodborne_pc](https://github.com/Supermedo/bloodborne_pc) | The port on Windows, the launcher in 13 languages, DLSS, FSR 3.1 and FSR 4 |
| [AdrianCsT/bloodborne_pc](https://github.com/0xCydral/bloodborne_pc) (now 0xCydral) | Windows releases 1.6 and 1.7: the redesigned launcher, Install from PKG, frame generation, XeSS, ReShade, the mouse camera and many fixes |
| Bloodborne PC Online community package | The LocalServer stand-in for the game's online services that `backend/` started from |
| [The Hunter's Dream](https://thehuntersdream.com) by droogie | The community server for Bloodborne's online content, whose service layout the backend follows |
| Kyo, Lance McDonald, auser1337, illusion, emoose and others | The community game patches for 1.09 (`bbport/patches/Bloodborne.xml`): frame rate, resolution, effects |
| [LukeYui's Elden Ring Seamless Co-op](https://www.nexusmods.com/eldenring/mods/510), Elden Ring Nightreign | The ideas: seamless co-op and party death (no code) |

Bundled in the release, unmodified unless noted: Intel XeSS (Intel license), NVIDIA DLSS
(`nvngx_dlss.dll`, NVIDIA's license; the bridge is adapted from
[IFreemz/shadPS4-Bloodborne-DLSS-FSR](https://github.com/IFreemz/shadPS4-Bloodborne-DLSS-FSR), MIT),
the AMD FidelityFX SDK and [FireBurn's FSR-Vulkan](https://github.com/FireBurn/FSR-Vulkan) (MIT),
[PkgTool / LibOrbisPkg](https://github.com/maxton/LibOrbisPkg) (LGPL-3.0),
[ReShade](https://reshade.me) (BSD-3-Clause) with effects by CeeJay.dk, AMD and haasn/JPulowski
(MIT), Qt 6 (LGPL-3.0, next to shadnet), and the libraries listed in
[bbport/README.md](bbport/README.md#credits-and-licenses). The release's `CREDITS.txt` and
`licenses\` folder carry them all.

## License

GNU General Public License v2.0 or later, like shadPS4 and the port it is built on
(`emulator/LICENSES/GPL-2.0-or-later.txt`, `bbport/LICENSE`). Third-party components keep their own
licenses (`emulator/LICENSES`, `server/LICENSES`, `bbport/packaging/windows/licenses`).

Bloodborne is a trademark of Sony Interactive Entertainment. This project contains no game code or
assets and is not affiliated with, endorsed or sponsored by Sony Interactive Entertainment,
FromSoftware, AMD, Intel or NVIDIA.
