# Bloodborne Seamless Co-op: Survey and Plan

Goal: a co-op mode modeled on Elden Ring Seamless Co-op. A party stays together across deaths,
lamps, area changes and bosses, with no bells or Insight, and story progress is shared.

Planning only. Nothing in the install was modified.

---

## 1. What is already on disk

Two installs: `C:\Bloodborne.PC.Online` (the live one) and `C:\Bloodborne.PC.Online - Copy`.
They are the same package. The launcher in the Copy still points at the original folder's exe
(`DATA\launcher\qt_ui.ini`, `versions.json` entry "NAZAR / Local").

| Piece | Where | Notes |
|---|---|---|
| Game | `DATA\Bloodborne\CUSA03173` + `-patch` | EU disc id CUSA03173, patch **01.09** (all hooks are pinned to 01.09) |
| Mods | `CUSA03173-mods\dvdroot_ps4\MODS` (JSGME) | visual/QoL mods only, nothing network related |
| Emulator builds | `DATA\launcher\versions\` | v0.16.0, v0.17.0, v0.18.0 (upstream), **Wozzardman's P2P fork** (2026-08-16), **"v0.17.1 - Bloodborne Fix"** (selected, built 2026-10-04 from `F:/ShadNet/shadp2p`) |
| Matchmaking server | `DATA\shadNet-P2P\shadnet.exe` + `shadnet.cfg` | Wozzardman's shadNet fork: accounts, Matching2 rooms, signaling, WebAPI, Bloodborne `SummonBroker` |
| Content backend | `LocalServer\server\bb_dummy_server.py` | Python replacement for thehuntersdream.com: ss.info, login, messages, bloodstains, ghosts, chalice glyphs, notices |
| Game patches | `DATA\user\patches\shadPS4\Bloodborne.xml` | standard community patches (60fps, res, debug menu enabled). No co-op patches here |
| Tools | Save editor, Chalice editor, Mouse/FOV injector | useful later for flag RE |

**No source code is on this PC.** The custom build was compiled on another machine (`F:` drive).
The source is public though:

- emulator fork: https://github.com/Wozzardman/shadp2p
- server fork: https://github.com/Wozzardman/shadnet-p2p

### Seamless work that already exists (from strings in the selected exe and the 2026-10-04 log)

The selected build's `bloodborne_re.cpp` installs native hooks on 01.09 when
`SHADPS4_BLOODBORNE_SEAMLESS_COOP=1` (the launcher .bat sets it):

- Beckoning / Small Resonant / Sinister Bell usable anywhere (area-flag, boss-cleared and "area complete" checks bypassed)
- active search and **cross-map candidate selection**: guest can join a host in a different map
- native summon construction with **host-placement transport** (`X-ShadPS4-Bloodborne-Host-Placement` header) and **guest stage handoff** (guest is warped to host's map before room join)
- matching is **retained across stage transitions**, with deferred native reload
- host lantern actions allowed in negative / area-complete SOS regions
- healing-fountain host-availability fix
- "summon arrival repair": guests stuck in flight > 30 s are closed through the native arrival handler (`SHADPS4_BLOODBORNE_SUMMON_ARRIVAL_REPAIR`)
- RE tracing and summon capture (`SHADPS4_BLOODBORNE_RE_TRACE`, `SHADPS4_CAPTURE_BLOODBORNE_SUMMON`)
- `/summon_messenger/*` WebAPI routed to shadNet (create/get/delete seen in the log)

Network shape today: shadNet on the host (`26.12.34.56:31313/31315`, a Radmin/VPN style address), game P2P over UDP directly between PCs, LocalServer on `:20443`.

### Things that look off

1. `shadnet.cfg` has `BloodborneSeamlessCoop=false`, while the client runs with seamless on.
   The shadNet side broker likely needs this true (or `SHADNET_BLOODBORNE_SEAMLESS_COOP=1`) for cross-map claims.
2. The last log has repeated `permission denied` opening `savedata\CUSA00207\SPRJ0005\userdata0001/0010`
   (a different title id's save). Probably harmless, worth checking once.
3. The log shows summon messenger traffic but no completed cross-map join, so I can't tell from logs whether the current seamless path actually works end to end for you.

**Bottom line:** what exists is "bells work anywhere and you can join across maps". It still uses Bloodborne's
native multiplayer session, so the session still ends on host death, guest death, boss kill, lamp travel,
Hunter's Dream, and Insight is still consumed. That gap is what this plan targets.

---

## 2. Target behaviour (Elden Ring Seamless as the model)

| ER Seamless feature | Bloodborne equivalent we want |
|---|---|
| Session persists through death | host or guest death: dier respawns at last lamp, party stays connected, auto-rejoin |
| No summon items | party joins via a menu/hotkey or auto-join on load; no Insight cost, no bells |
| No boss-room / area limits | play anywhere incl. Hunter's Dream, cleared areas, Chalice dungeons (stretch) |
| Session persists after boss | boss kill does not dismiss guests |
| Travel together | host lamp warp / Dream travel pulls guests along |
| Shared progress | boss kills, shortcuts, lamps lit for guests too (configurable) |
| Each player keeps own world/save | guests always load into host's world; their own save keeps their character + progress grants |
| Separate seamless save | seamless uses its own save slot so vanilla saves are untouched |
| Party HUD / names | phantom names, HP bars (native phantom UI is fine for v1) |
| Config file | `seamless_coop.ini`: password, enemy HP scaling, PvP on/off, shared-progress rules |

Non-goals for v1: invasions/PvP, more than 4 players, Chalice dungeon co-op in different glyphs, crossplay with real PS4.

---

## 3. Architecture

Recommendation: **build on the existing forks** (shadp2p + shadnet-p2p), not raw shadPS4 patches.

Why not `.xml` patches: they are static byte patches. Seamless needs runtime logic
(re-join after death, warp coordination, flag sync), which needs code calling native game functions.
The fork already has the hooking framework (signature-checked detours on the 01.09 eboot, native-call wrappers).

```
 ┌───────────── PC A (host) ─────────────┐        ┌──────────── PC B (guest) ────────────┐
 │ Bloodborne 01.09 (guest code)          │        │ Bloodborne 01.09                      │
 │   ▲ detours / native calls             │        │   ▲                                   │
 │ shadp2p: bloodborne_seamless/*.cpp     │◄─UDP──►│ shadp2p: bloodborne_seamless/*.cpp    │
 │   - SessionController (state machine)  │  game  │   - SessionController                 │
 │   - FlagSync / ProgressSync            │  P2P   │                                       │
 │   - SeamlessChannel (side channel)     │◄─────► │   - SeamlessChannel                   │
 └──────────────┬─────────────────────────┘        └──────────────┬────────────────────────┘
                │ Matching2 / WebAPI                               │
                ▼                                                  ▼
         shadnet-p2p: SummonBroker → **PartyService** (party id, members, host, map, password)
         LocalServer (unchanged; content only)
```

Three layers:

1. **Game hooks (emulator side, C++ in shadp2p).** All seamless logic lives here, gated by
   `SHADPS4_BLOODBORNE_SEAMLESS_COOP` and the 01.09 version check. New module split:
   `session_controller`, `death_hooks`, `warp_hooks`, `flag_sync`, `party_ui`.
2. **Side channel.** A small reliable message channel between party members, separate from the
   game's own P2P packets: party events, warp requests, flag deltas, keepalive. Options: extra
   message type on the existing shadp2p UDP transport (preferred), or a shadNet websocket relay as fallback.
3. **Server (shadnet-p2p).** Grow `SummonBroker` into a persistent **party**: created once with a password,
   survives Matching2 room teardown, remembers host, members and host's current map, and re-issues
   claims automatically when someone needs to re-join. LocalServer stays as is.

Core trick: we don't try to keep Bloodborne's native session alive through every event at first.
We let it end and **automatically and invisibly re-establish it** (party remembers who's in, host
re-opens the room, guests auto-claim with host-placement). That's how the existing cross-map path
already works. Later phases suppress the teardown itself where that proves feasible.

---

## 4. Game state to sync

| State | Owner | Mechanism | Phase |
|---|---|---|---|
| Party membership, host id, host map/lamp | server | PartyService | 1 |
| Player transforms, animations, damage, enemies, boss AI | host (native) | Bloodborne's own phantom netcode, unchanged | – |
| Death / respawn events | dier | side channel → controller | 2 |
| Lamp warp, Dream entry/exit | host | side channel, guests follow via host-placement | 3 |
| Event flags: boss kills, shortcuts, doors, lamps, NPC quest steps | host world | flag deltas → guests apply to own save (whitelist) | 4 |
| Item pickups | each player | keep native (host world items vanish for host only); optional duplication for guests | 4 |
| Blood echoes / Insight on boss kill | native | already granted to phantoms; verify | 4 |
| Enemy HP scaling | host | param patch at session size change | 5 |

Do NOT sync: inventories, stats, other players' saves, chalice glyph state (v1).

---

## 5. Open reverse-engineering questions

Most can start from the shadp2p source and its RE trace hooks plus the Debug Menu patch already enabled.

1. **Session teardown points:** which 01.09 functions dismiss phantoms on (a) host death, (b) guest death,
   (c) boss kill (the "Hunter returned to their world" path), (d) lamp use, (e) Dream entry,
   (f) leaving the area. Can each be suppressed, or only re-joined after?
2. **Phantom death:** can a dead guest be revived in place (like ER's respawn-at-grace while staying in host world),
   or must they go home and auto re-summon? Find the "return to own world" trigger.
3. **Warp:** the stage descriptor (`Warp.StageDescriptor.Finalize` is already traced). Can a guest be
   warped to an arbitrary lamp in the host's world without a full summon handshake?
4. **Event flag layout:** address of the event flag manager in 01.09, flag ids for every boss / lamp /
   shortcut (the Save Editor and community flag lists help), and whether flags written mid-session persist to save.
5. **Insight cost and bell animation:** where the bell consumes Insight, and whether the summon can be
   kicked off without the item use at all (menu/hotkey).
6. **Fog walls:** guests can't enter boss fog before host; find the phantom fog-wall gate.
7. **Separate save:** redirect save path to a seamless slot when the mode is on (emulator side, simple).
8. **Max players:** Bloodborne caps at host + 2 cooperators; is the cap a single constant?
9. **Chalice dungeons:** they use a separate glyph matching path; out of scope until 1–6 are done.
10. **Stability:** "summon arrival repair" exists because joins hang. Root cause, or keep the watchdog?

---

## 6. Milestones

**Phase 0: Baseline (setup, no new code)**
- Clone shadp2p and shadnet-p2p, build both on this PC (MSVC/clang + Qt 6), reproduce the selected "Bloodborne Fix" build.
- Fix the config mismatch (`BloodborneSeamlessCoop=true`), confirm a cross-map join works between two PCs.
- Two-instance local test rig (two shadPS4 instances on one PC with separate user dirs and `SHADPS4_P2P_PORT`).
- Exit: reproducible build plus one working cross-map join recorded in a log.

**Status (2026-10-05):** Phase 0 is done: two-instance rig, our build, cross-map join. Phase 1 is
done in the rig: party-scoped summons, auto-ring of both bells, and rejoin after a death within
seconds (5 s summon polling, 5 s co-op guard timers, stale-claim reset). See
[PHASE1_PARTY.md](PHASE1_PARTY.md).

**Phase 1: Persistent party**
- PartyService in shadNet: create/join with password, survives room teardown.
- Auto-rejoin: when a guest is sent home for any reason, the client re-claims the host automatically.
- Exit: guest gets sent home by any cause and is back in the host's world without touching a bell.

**Phase 2: Deaths don't end the party**
- Guest death → respawn at own lamp → auto-rejoin (ER behaviour) or in-place revive if RE question 2 allows it.
- Host death → host respawns, re-opens room, guests pulled to host's lamp.
- Exit: 10 deaths in a row without anyone leaving the party manually.

**Phase 3: Travel together and no limits**
- Lamp warp / Dream entry by host pulls guests along.
- Boss kill no longer dismisses guests; summons allowed in cleared areas and Hunter's Dream.
- **Bell-less joining.** Today the party controller "rings" bells through the native use-item
  path, so the game still plays the animation, uses the item and charges Insight. Instead, call
  the layer under the bell directly: start the SOS/summon search state that the bell effect
  (9001/9005) turns on, without the item. The goods-use and effect hooks the RE trace already
  logs are the starting point. The party then reconnects with no item, no animation and no
  Insight, and the bell items become optional.
- Exit: full route Central Yharnam → Cleric Beast → Dream → Father Gascoigne without re-summoning by hand.

**Phase 4: Shared progress**
- Flag sync whitelist: bosses, lamps, shortcuts. Separate seamless save slot.
- Exit: guest's own save shows the boss killed and lamp lit after the session.

**Phase 5: Polish**
- `seamless_coop.ini`, enemy scaling, party HUD/names, 3–4 players, packaging into a launcher .bat
  in the same style as the existing ones.

Phases 1–2 are the riskiest RE; 0 is mostly build plumbing.

---

## 7. Decisions needed before Phase 0

1. Build on Wozzardman's shadp2p/shadnet-p2p source (recommended) vs. from upstream shadPS4.
2. Can I install a C++ toolchain here (Visual Studio Build Tools / LLVM, CMake, Qt 6) to build it?
3. Who tests the second player: a friend's PC over the existing VPN, or two instances on this PC?
