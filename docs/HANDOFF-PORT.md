# Handover: seamless co-op on the native port (2026-10-09 evening)

Kaniel's direction: the native port (Bloodborne PC, `bbport/`) is the main target; shadPS4 is to
be dropped once the port works well. Port-only solutions are welcome when they make a co-op
mechanic simpler or more reliable. No mixed parties (port and shadPS4 players never meet). Never
leave/rejoin a session as a fix: everything is seamless. See docs/BBPORT.md for how the port and
its co-op are wired, built, packaged and tested.

## Sync fixes and a ping on the rig (branch sync-fixes, 2026-10-10)

Kaniel's playtest on the rig (play-down-revive): the guest saw Iosefka at her clinic door while
the host's world hid her, and on new characters the dog 35 m from the first lamp died by itself
in the host's world, its loot on the ground.
- **A ping on the rig** (default 50-100 ms round trip, `--ping 0` for none): each game holds what
  it sends the others half the ping (`SHADPS4_P2P_DELAY_MS=25-50`, `net_delay.h`, in the P2P
  transport's send; packets keep their order), and the runner puts delaying TCP proxies 1000
  ports above shadNet (32313), its web API (the party boards, 32315) and the backend (20453)
  (`tools/rig/latency.py`). The games derive shadNet's STUN port as the TCP port + 1, so with a
  ping the runner sets the rig's `shadnet.cfg` `MatchingUdpPort` to 33314 (UDP is not proxied:
  shadNet must see each game's own address). The first try without that: no join (endpoint
  unresolved).
- **Iosefka / story NPCs**: a guest's world, rebuilt as the host's client at its join, does not run
  the events that hide story NPCs, so it showed them. The host now posts every character of its
  map with its event disable byte (+0x28) to a new board, `POST /bloodborne/party/shown` (map
  state board format, `EncodeShownCharacters`), and a guest in the session on that map sets its
  copies the same (`ShownLikeTheHost`, skipping those its kill board hid). On the rig the guest
  hid 6 characters the host hides (Iosefka 0x10014005, 0x100140ae, the dog 0x10014009, ...).
  Scenario story-hidden. The rig's shadNet needs this build (`server_is_current` probes /shown);
  so do the kit's.
- **The dog**: before the session the guest's own world posted its hurt dog to the HP board and
  the host's dog was lowered to that (rig hurt-before-join, red: the host's dog at 5 HP before
  the join); Kaniel's host then got +235 echoes and the loot the moment the session formed. Now
  only damage done in the session is posted or applied (`VisitDamage`: none out of a session,
  each enemy counted from its HP when the session or the visit began, or the most since).
- **Invisible guest after the host's trip** (Kaniel, hp-after-host-trip with the ping): the host left for the
  Dream while its join task for the guest was at step 3 (data not synced, flags 0x4); back on the map
  it rebuilt the guest from that save into the character step with the data marked synced: a
  "Beckoner" name plate and no body. Saved tasks now keep their step, and a member still joining is
  rebuilt as the game's own fresh join (`RebuildsFromSave`). Not yet rig-confirmed (Kaniel stopped
  the rig): rerun hp-after-host-trip with the ping and look at the guest on the host's screen.
- Kit 2026-10-10 17:11: C:\bb\port-kit-sync.zip (servers from C:\bb\out\sync-kit-servers = online-kit
  + the new shadNet). The host's HOST bat runs ..\SeamlessCoop\server\shadnet.exe when it is there:
  that copy needs the new shadnet.exe too.
- New rig verbs: `census [<meters>]` (every character that close: HP, death flags, hidden,
  disabled, position) and `expect-hidden <handle> <0|1> [<timeout>]`. New scenarios:
  story-hidden, hurt-before-join, clinic (both players by Iosefka's door, censuses),
  guest-flags-stay (a guest's own flag sets do not reach the host's world: passes).
- Checked and not the cause: the game does not pass a guest's own event flag sets to the host
  (guest-flags-stay). Kaniel's host did get Forbidden Woods flags (12700130...) late in that run,
  9 s after the guest's world set them again; where from is not known yet.

## Harder enemies (branch harder-enemies, 2026-10-10)

Kaniel: bosses are too weak for a party (poise-locked and R1-spammed), so every enemy and boss
gets more HP, damage and poise. `SHADPS4_BLOODBORNE_HARDER_ENEMIES=<hp>,<damage>,<poise>`
(kit SETUP asks, default `3,1.5,3`, `off` for vanilla) scales the loaded tables once, from the
title screen on (`MakeEnemiesHarder`, rules in `bloodborne_party` + tests):
- NpcParam `hp` (+0x20) x hp; 1-HP event dummies stay.
- NpcParam `superArmorDurability` (+0x118, poise) x poise, with a floor of 15 (50 for base HP
  1000+) x poise, since most enemies and bosses (Gascoigne: 0) have none; 9999 stays.
- AtkParam_Npc `atkPhys/Mag/Fire/Thun` (+0x50..0x56) x damage. AtkParam_Pc has the same type
  and is told apart by row 271000 (Gascoigne's first axe swing). Hostile NPC hunters that fight
  with player weapons use AtkParam_Pc and are not scaled yet.
- One HP multiplier for bosses too (Kaniel): the game's co-op boss HP bonus (SpEffect
  7500-7503 maxHpRate x1.5-x3) is set to 1 and KeepBossHpInStep stands down; the rows' status
  resistance bonus stays.
- Shared enemy HP is synced as a share of max HP, so a member with another setting stays in step,
  but everyone should use the same setting. Not checked yet: whether poise > 0 really stops the
  stagger on every hit for enemies that had 0 (rig + playtest).
Offsets from Paramdex BB defs, checked against the 01.09 gameparam (row size 0x184 / 0xA4,
Gascoigne 1440 HP, poise 0, first swing 136 physical).

## State 2026-10-10 early morning

Rig on 3f730dc0 and later: guest-crash, host-crash, resume, crash-apart, boss-kill,
host-death-boss, join3, crash3, guest-trip3, trip3 (after the fix below), crash-rejoin-hp,
guest-talk-at-lamp, guest-talk-by-the-lamp, pickup, pickup-after-crash, hp-across-blocks,
guest-death-seen and echoes-after-crash pass.

- Fixed: a guest that went to the Dream lost the slot of the guest it left behind (its trip
  back saved only the tasks found in the Dream), so back on that guest's map the rebuilt task
  had no slot and the two guests never saw each other (trip3). MergeSavedJoinTasks keeps the
  saved tasks of members not met on this map.
- crash-rejoin-hp checks only the host's hit: in a session the host's game owns its enemies' HP
  and puts a guest's direct write back within seconds (the rig's hurt on a guest is no hit).
- New rig verb `pickup [<meters>] [<timeout>]` (onto the nearest usable map item, X until it
  is gone); `teleport` and `approach` take a heading; snapshots give nearby positions. Never
  pick up far items: a jump 21 m down from the lamp item drops the player through the floor
  and crashes the game in 0x0191B6E0 (a character lookup by entity over a block's list), with
  or without a crash before (pickup-far, removed).
- Forest pickups (Kaniel's log, 2026-10-09): the item button kept finding items but no press
  ever reached the pickup (0x01CD91F0, the action button check, said no) from the time-of-day
  reload into the Forbidden Woods (a trip that kept the session) on, before the friend's crash.
  Not reproduced: pickup-after-crash, pickup-after-trip (both to the Dream and back) and
  pickup-after-reload (in-place warps) all pick up. Next: a real time-of-day reload, or the
  item there (0x52 by the start of the woods carried flags 0x41, the others 0xf841).
- Fixed: a guest talking to Gilbert at his window by the Central Yharnam lamp (talk 241319, 3 m
  from lamp 241001) went to the Dream. His talk sets 70000140, which IsLampTravelFlag took for a
  lamp's trip (any 7MMM01LL but the Dream's); now only map codes 211-399 count (the 799001xx
  many NPC talks set are out too). Rig guest-talk-gilbert reproduced it (heading h faces
  (-sin h, cos h)).
- Join stalls (no session for minutes, both sides waiting): 4 of 19 joins from 01:12 to 02:52 on
  2026-10-10, against about 1 in 60 before; 7 of 7 again once the per-frame talk-position hook
  (0x0204C63B, every talk every frame) and the guest's per-frame talk-start log came out (kit
  3bc22a7f still had them). Two shapes: the guest got the claim but never ran the host's
  matching check (OnMatchingCheck 0x013808B0), so it never removed its sign
  (SummonDataRemoveRequest) or called JoinRoom; or the host made its room but its queued summon
  never started (SummonSelection.QueueInsert without RequestQueued 0x01873605, which needs
  MultiPlayMan +0x124 == 3, [rbx+0x78] and [rbx+0x90] == -1). After a stall the host keeps the
  sign in its pending candidates (list at [manager+0x1A8], entry +0x10 state) and never asks
  again. A guest re-ring did not help (the sign keeps its SessionId) and was taken out again.
- Enemy HP after trips (Kaniel 2026-10-10, "not shared after tp's, I think"): hp-after-host-trip
  and hp-after-guest-trip pass, the game's own sync both ways within 2 s after either trip
  (the guest's real hit via the new rig verb `hit`). The boards cover members apart: lowest HP
  wins there, so two members hitting their own copies apart do not add up. A first failure was
  the test's timing; step lines now carry t= (one clock for both games).
- A host's warp to another lamp of its own map (pickup-after-reload, the first try of
  guest-talk-gilbert) makes the guest's game drop the room for a moment; players cannot do that
  (lamps lead to the Dream), so the scenarios no longer do it.
- lamp-after-crash fails: a lamp the guest lit in the session reads unlit in the host's world
  after the guest's crash and rejoin (its own world has it: the progress file sets it at
  load). The flag board gives a new game nothing older than its first post (a new character
  must not get old lamps), and the host posted that lamp before the crash.

## State 2026-10-09 night (branch merge-05b, to become bbport)

Done (Tasks 2-6, merge first): deadinside28 0.5-pre3 merged onto the AdrianCsT base (0.5's
settings pages in the game's System menu with Adrian's XeSS/frame generation; co-op keeps the
PLAY ONLINE/OFFLINE screen, 0.5 skips it into offline otherwise). The 0.5 PC memory model runs
on Windows (host import of the runtime's section views, game code hooks with System V ABI;
opt-in BB_PC_MODEL=1; free_check/gnm_hooks diagnostics still stubbed). Fixes: staging pool lock
(the friend's RTX 3070 crash dump: RecordHostCopy -> RunGuestCopy -> Buffer::Flush into a
dangling staging buffer), enemy HP shared across blocks of one area (the guest rejoined in place
on another block), item-button and remote-death diagnostics on in every co-op game. Tried and
reverted (both broke joins in the rig): 1 s sign polling (the guest got a claim every second
and never joined the room) and skipping the game's 5 s arrival wait at 0x01E4B09F (the guest
dropped the host's connection ~5 s after it formed: lone-guest, host-crash). Rig tests run in
the background now (no focus taken). Full run 2026-10-09 (stopped at Kaniel's word): 15 passed
before the two failures; lone-guest and host-crash pass again without the patch; the rest of
the set (resume onward, the new scenarios, join3/trip3/crash3, soak) is still to run. Save tab in the settings menu (BbSaveEditor: echoes, insight, attributes through
0x1c13d00, items through 0x131cb70 on the game's thread from its pad reads; item list from the
shadPS4 kit's DATA/Bloodborne Save Editor/resources). New rig verb expect-others-near and
scenarios hp-across-blocks, echoes-after-crash, lamp-after-crash, crash3, host-death-boss, soak.

Open: invisible guest after respawn and host pickups after a guest crash have no cause yet
(the new diagnostics and guest-death-seen should show it); a fixed ~6 s post-load phase before
events start (paid twice per join, not found yet); the save editor's offsets were read from the
eboot, check the "Save editor: level ..." console line once in the world.


- `bbport` (pushed, 0bc9a1c9 plus this plan): our co-op port on Supermedo's Windows 1.6 with
  **AdrianCsT 1.6.16 merged in** (deadinside28 0.4 on Windows and 64 Windows fixes, among them
  the in-game menu clicks and the SnormNz crash our players hit). Launcher renamed: `BLauncher.exe`
  is the launcher (and runs `--run`/`--script`), `Bloodborne.exe` starts the game directly.
- `bbport-0.5-wip` (pushed, af10e3ba): deadinside28 0.5-pre3 merged into the pre-Adrian `bbport`
  with every conflict resolved, **not built and not ported** (see Task 5).
- Not pushed: `emulator/tests/gcn/test_pixel_format.cpp` (+ CMake line) in Kaniel's PC clone
  C:\bb\port-src (jj working copy): the emulator copy of the SnormNz fix still needs its red run,
  the fix (`emulator/src/video_core/amdgpu/pixel_format.h`, same change as bbport's) and
  `ci-check -Component emulator` (build into a separate out dir: `build-emu.ps1 -Out
  C:\bb\out\port-emulator`; C:\bb\out\emulator belongs to the main checkout).
- Kit: C:\bb\port-kit.zip (176 MB) from `tools\make-port-kit.ps1` on the merged build: DLSS (from
  upstream 1.6's release), FSR 4 shaders, XeSS, PkgTool, ReShade; skips Vertex Explosion Fix.
  Checked end to end on two throwaway installs (kit SETUP, its servers, PLAY via `BLauncher.exe
  --run`): host and guest joined, the Cleric Beast died, the session held.

## Test status on the merged build (0bc9a1c9)

- Port tests: coop-hooks, log-file, pixel-format green; `ci-check -Component bbport` and `tools`
  green; boot check: 14 co-op parts in.
- Rig scenarios passed: boot, join, trip, kill-apart, dog, hp-apart, boss-kill (through the kit).
  Not yet run on this build: boss-refight, lamp-share, join-ward, join-apart, join-after-trip,
  lone-guest, echoes-apart, host-death, guest-death, host-crash, guest-crash, resume, crash-apart,
  host-crash-apart, join3, trip3, and the new crash-rejoin-hp and guest-death-seen.
- The rig now starts the port's games side by side (`Engine.staggered_start`); confirm with join
  first (it was only ever run staggered).

## Tasks

Order (Kaniel, 2026-10-09 20:23): **merge and fix everything first, test at the end.** Do Task 5
(upstream 0.5 on Windows, merged onto this branch) first, then Tasks 2, 3, 4 and 6 (fixes, join
speed, new scenarios and verbs, menu), with only the fast checks along the way (unit tests,
`ci-check`, the port's tests, a boot check). Then Task 1, the full rig run over everything, and
fix what fails; then Task 7.

1. **Full rig run, last.** `py -3.12 tools\rig\run_scenario.py --engine port <all>`
   (start detached; logs in C:\bb\rig\runs\<when>\<scenario>, shadnet.log has timestamps). Only
   when Kaniel is not playing; never stop a game that is not the rig's own (C:\bb\out\bbport).
   Kill the runner with `taskkill /T` (killing `py.exe` alone leaves python and its games).
2. **Bugs from the friend playtest (2026-10-09, both on the port kit)**, each with a scenario
   first (red), then the fix (green):
   - *HP bars apart after the guest's crash and rejoin*: after the rejoin the host never lowered
     an enemy to the guest's HP again (ShareEnemyHp needs the guest's /party/hp posts; check the
     guest side: PartyTripActive stuck after the resume? map/handle mismatch?). Scenario
     `crash-rejoin-hp` (new). Logs: the host's prev log of that evening is gone; reproduce.
   - *Invisible after respawning*: the guest died in the host's world and got up invisible to the
     host. Scenario `guest-death-seen` (new; wait-members checks the host has it on its map). If
     it passes, extend: hurt the guest's character from the host side, or snapshot positions.
   - *Host cannot pick up items after the guest's crash*: pickups allowed (GlobalState +0x1592)
     the whole time; lead: after the time-of-day reload the nearby map items carried flags 0xe041
     instead of 0xf041 (bit 0x1000 gone). Needs an autoplay verb to pick up the nearest item (the
     item-button diagnostics, InstallItemButtonDiagnostics, need SHADPS4_BLOODBORNE_RE_TRACE=1)
     and a scenario: guest crash, rejoin, host picks up an item on its map.
   - *NVIDIA RTX 3070 laptop (8 GB) crash*: texture cache memory pressure up to 5197 MiB
     (critical 5075), 125 images evicted, then a null read in nvoglv64.dll on bb:VkRecorder.
     Check whether AdrianCsT's fixes change it; otherwise eviction of in-use images is the bug.
     Players can lower memory: "! Downscaled 50% Textures" mod, model detail lower.
3. **Join speed.** In the join test the guest posted its sign ~30 s after it was in the world
   (shadnet.log: host searches from 19:33:14 every 5 s, guest's create at 19:33:57, claim at
   19:34:00, signaling at 19:34:01). Time the guest from "in the world" to "auto-rejoin started
   search effect=9005" (the logs have no timestamps: watch the file, see the scratch boot_timing
   idea in the session) and cut the wait. Menus cost ~6 s of an 11 s start; skipping them
   (call the game's continue, or 0.5's no-online-screen) is optional.
4. **More scenarios** (all with existing verbs unless noted): echoes and kill board after a
   crash-rejoin; lamp lit by the guest during the session survives the guest's crash;
   three-player crash of one guest; host death during a boss fight; a long soak (30 min of
   trips) for leaks; menu settings saved and kept (needs real input, see Task 6); item pickup
   (new verb, Task 2).
5. **Upstream 0.5 on Windows** (when 0.5 is a release, from `bbport-0.5-wip`, or re-merge onto
   Adrian's base): its new modules are Linux-only and need Windows ports or `_WIN32` stubs:
   `gpu/shim/bbport_guest_memory.cpp` (memfd/dma-buf: return Usable()=false on Windows),
   `bbport_guest_hooks.cpp`, `bbport_game_menu.cpp`, `bbport_gnm_hooks.cpp` (mmap near the
   image, mprotect, SIGTRAP: use VirtualAlloc/VirtualProtect and a vectored handler as
   `bbcoop/guest_hooks.cpp` does; `std::aligned_alloc` -> `_aligned_malloc`),
   `bbport_free_check.cpp`, `bbport_wait_trace.cpp`, `buffer_cache_layer.cpp`,
   `page_manager.cpp` (unguarded Linux headers), `vk_rasterizer.cpp` (dladdr),
   `shim/common/assert.cpp` (weak extern, pause), `src/runtime_content.c` (openat/fdopendir),
   `src/runtime_savedata.c` write_atomic (rename over an existing file fails on Windows).
   Every function the game calls into (detours, wrappers) needs `PS4_SYSV_ABI`. Check that the
   game menu and GNM hooks do not overlap the co-op hook sites, and that dropping the
   online/offline screen keeps co-op working. The NVIDIA memory module relies on dma-buf, so on
   Windows NVIDIA keeps the default memory model unless it is ported.
6. **In-game menu**: confirm Adrian's fix on this PC (150% scaling): a normal click toggles a
   checkbox (`ui.py`-style real input: SetCursorPos/mouse_event with a DPI-aware process; the
   frame dump does not show the overlay, capture the screen). Then the debug menu: on 1.6 the
   patch went in and the touchpad reached the game but nothing drew; 0.5 says the menu needs
   `adhoc/FontShader/debugFont_vs.vpo`/`_ps.ppo` too (Kaniel's install has them). If it still
   does not draw, Kaniel was offered a save editor page in the port's own menu (echoes, insight,
   stats, items) instead; ask before building it.
7. **Kit and release**: rebuild the kit after fixes (`bash packaging/windows/package.sh` with
   WINPYTHON=python.org 3.12; then `tools\make-port-kit.ps1 -Package
   <bbport>\out\stage\bbport-windows`), check it end to end on throwaway installs (fake install
   with a DATA\Bloodborne junction to the real one, the zip expanded, SETUP answered by pipe,
   the kit's servers, PLAY with SHADPS4_BLOODBORNE_AUTOPLAY), hand C:\bb\port-kit.zip to Kaniel.

## Notes that cost time today

- The Windows clone has CRLF files (global core.autocrlf=true); jj needs
  `working-copy.eol-conversion=input-output`, git commits need `commit.gpgsign=false` locally.
- Packaging needs `WINPYTHON` (it otherwise picks Python 3.9 from the user's programs).
- Killing the rig: `taskkill /T /F /PID <python>`; also the rig backend on port 20453 and the rig's
  shadnet in C:\bb\rig\server.
- Kaniel's servers run from the port kit's HOST script, which starts the shadPS4 kit's servers
  (C:\Bloodborne.PC.Online - Copy\SeamlessCoop\server, its db holds the accounts).
