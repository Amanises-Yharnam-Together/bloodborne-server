# Handoff: state on 2026-10-06 (after lamps as campfires)

Start here when picking the work up in a new session.

## Where things are
- Repo: https://github.com/lKaniel/bloodborne-server, a jj checkout at `C:\bb\bloodborne-server`.
  `emulator/` = Wozzardman/shadp2p and `server/` = Wozzardman/shadnet-p2p (subtree imports, full history).
  `backend/` = local ss.info/content server (Python). `tools/` = build, CI and rig scripts.
- Builds go to `C:\bb\out\{emulator\build, emulator\tests, server}`. LLVM 20.1.8, VS 2022 BT, Qt 6.8.3 at `C:\Qt`.
- Rules: jj for VCS, red→green TDD, `tools\ci-check.ps1 -Component emulator|server|backend` green
  before every push. Commit signing is off for this repo only.
- Commit messages: write the message to a file and use `cmd /c "jj describe --stdin < file"`.
  PowerShell piping adds a BOM, and `-m` with embedded quotes breaks.

## Test rig (`C:\bb\rig`)
- `tools\rig-setup.ps1`, then `tools\rig-lowspec.ps1`, creates it: private shadnet on 32313-32315,
  backend on 20453, and host/guest dirs. Accounts are rig_host / rig_guest, password "rig".
- `host\launch.bat` / `guest\launch.bat` set seamless, party `rig:hunters`, role and RE trace.
  The RE trace (`SHADPS4_BLOODBORNE_RE_TRACE`) is now **0** in both: its ~150 hooks on hot code
  made two instances lag. Set it to 1 only while reverse engineering.
- Restart the servers with logs in files (Start-Process with -RedirectStandardError
  `server\shadnet.err` / -RedirectStandardOutput `backend\backend.log`); `start-servers.bat` logs
  to console windows only.
- Low-spec profile: 540p, Potatoborne only (through a junctioned `rig\game` folder), no extra dmem.
  RAM commit is still close to the limit with two instances.
- Logs: `rig\<p>\user\log\shad_log.txt`, `rig\server\shadnet.err` (timestamps),
  `rig\backend\backend.log`, RE captures in `rig\<p>\user\captures\bloodborne-re\*.jsonl`.

## Done
- Phase 0: our build, two instances, cross-map join.
- Phase 1: party header (`SHADPS4_BLOODBORNE_PARTY`, server `NormalizePartyKey`), party-scoped
  broker, `PartyController` auto-rings the role bell (host Beckoning goods 200 / effect 9000; guest
  Small Resonant 205 / 9005) from the SOS-status hook, backend `SyncCharaId`, 5 s summon polling
  and co-op guard timers, stale-sign supersede and claim reset after join. User-confirmed:
  rejoin after a death is near-instant.

## Phase 3 item: bell-less joining (done, confirmed by Kaniel in the rig 2026-10-05)
- RE finding: a rung bell is just special effect 9000 (Beckoning, game adds 9003 too) / 9005 (Small Resonant) on
  the player. The SOS status (`0x0186FE40`) derives "searching" from the effects' special state.
  01.09 adds 9005 itself at `0x01C0D3B0` through the character's add-effect virtual
  (vtable slot `0x3F0` = `0x018C6DB0`), args `(chr, id, chr, 0, 1, 0, 1.0f x5, stack 0)`.
- `StartSummonSearch` (bloodborne_party.cpp) makes that same call. Party auto-rejoin and the
  cross-map responder resume both use it instead of `UseItemNativeApply` (goods 200/205), so no
  item, animation or Insight is involved. The slot is checked against the verified 01.09
  function before calling.
- Log line: `auto-rejoin started search effect=9000|9005 ... result=true`. Note the host's
  effect is 9000; 9001 (used in Phase 1 code and still in the healing-fountain host check) is
  not a search effect, so that fountain fix has likely never fired.
- Eboot RE on this PC: scratch scripts flatten `eboot.bin` (fake SELF) and apply relocations,
  then disassemble with Python capstone (`py -3.12 -m pip install capstone`).

## Session teardown (2026-10-05)
- The seamless keep-matching patch (call at `0x019471B1` in the world teardown `0x019470F0`)
  kept the session over *every* teardown, so a guest quitting to the title stayed in the room.
  A hook at `0x0194716E` (`0x01947167` is RIP-relative and cannot be hooked) now calls the
  native `StopRequest` when the guest is summoned (state 6) and no summon reload is running.
  Phase 2 extended it to the host (state 3): a host quitting to the title ends the session, so
  guests are not left in a room with a ghost host. Confirmed: guest quit-to-menu and reload rejoins.
- Server: signs not refreshed for 75 s are no longer offered (stale sign from a previous run).
- Sign timers: 01.09 waits 5 s before a sign's first post and 30 s between re-posts
  (`0x018A9264/9324`, `0x018A9757/988F`), ignoring ss.info. Patched to 0 s / 5 s.
- Known: the emulator's signaling reuses a lingering (locally deactivated) connection when the
  peer re-activates within 60 s; the game never hears that invite.

## Phase 2: deaths that don't disconnect (done, confirmed by Kaniel in the rig 2026-10-05)
Kaniel's design: nobody leaves on death and the world does **not** reset. Host and guest deaths
work the same: YOU DIED, echoes dropped in a bloodstain, get up, teleport to the player's own
last lamp. Both sides keep seeing and hitting each other; repeated deaths work.
- **The death flow is native.** 01.09 runs a compiled copy of the event scripts; the Lua source
  (`global_event.lua` in `script/eventcommon.luabnd.dcx`) is never loaded, but it is the best map
  of the native code (state names such as "PartyGhostDeath_wait" are string refs in the eboot).
  `py -3.12 tools\re\lua50dis.py <eventcommon.luabnd.dcx> [function regex...]` disassembles it.
  Event script bindings (name -> native function) were extracted from the registration code
  around `0x01340000` (e.g. RevivePlayer `0x01332B20`, SetChrType `0x01338DF0`).
- **Guest death** (bloodborne_party.cpp `GhostDeathKeepSessionPatches`, `ReviveDeadPartyGuest`):
  PartyGhostDeath (`0x01381FD0`) no longer sends 4063 (the host's OnGameLeave drops the guest),
  sets 4047 or SetFlagInitState(2); PartyGhostDeath_1 skips the "returned to your world" menu
  and warp. A hook at `0x01382354` revives instead: clear death flag 4000 and the "returning"
  state ([SprjEventMan+0x10]+8, set to 4 by OnEvent_4000 `0x013813D0`, blocks later deaths),
  RequestFullRecover, UpDateBloodMark (with GlobalState +0x1592 set so the stain uses the death
  position), RevivePlayer, 01.09's local-player revive `0x013179B0` (clears the death's
  controller locks [+0x58]+0x114 / [[+0x58]+0x38]+0x48: no move, no damage, not targeted),
  restore saved alive flags, SetChrType(white) (the revive makes a live player), ChrResetAnimation,
  then the lamp teleport. The self-bloodstain pickup (OnSelfBloodMark 4032) is registered for
  ATTR_WHITE too (`0x0137EBC7`), so guests can pick up their stain in the host's world.
- **Lamp teleport**: GlobalState +0x1528 holds the last lamp's respawn point id (2412951 etc.);
  `0x0154AFC0` resolves it against the loaded blocks ([`0x0553B148`]), then the character's
  vtable +0x5F8 moves it (as the Warp binding does). Only works if the lamp's block is loaded;
  otherwise the player gets up where it fell.
- **Host death** (`ReviveDeadPartyHost`): new `InstallGuestFunctionOverride` in cpu_patches (a
  hook at a function entry that can skip the function and return 0). Native SoloPlayDeath_2
  (`0x01381DE0`, bloodstain + reload at the lamp) is replaced while hosting (state 3) by the same
  revive, minus the white checks. Guests skip HostDead's warp home (`0x013819C6`), and the death
  step no longer removes session members from a dying host's world (`0x018C4D1D`). Solo deaths
  are vanilla.
- Dead ends, so nobody retries them: an in-place RevivePlayer alone (stays in its death state);
  a guest summon reload as a respawn (the host's half of the summon handshake never runs, the
  guest leaves the room ~12 s later, and it always lands at the original summon spot); the Warp
  binding with the respawn id (it wants a map entity).
- Diagnostics (logged on change, cheap): `bloodstain state`, `other players`, `local effects`,
  `revive restored alive state`, `warp to last lamp` / `moved to last lamp`.

## Lamps as campfires (done for now, rig-tested by Kaniel 2026-10-06)
Kaniel's design: a rest works "like a reload, but in place, without breaking the session". In a
party session (party configured and matching state 3 or 6) only:
- **Lamps work in a session.** The lamp talk scripts (t<map><nnn>.esd, 46 lamps; see `tools\re\esdump.py`) hide a lit lamp while ESD function 52 ("in multiplayer": session
  players > 1 or a pending matching) is true. A register hook at `0x0203C55F` (ESD function
  dispatcher `0x0203AD60`, case 52) makes it false. The healing fountain's +0x49 byte is cleared
  for both roles (`0x012F836E`, previously host-with-9001 only).
- **Using a lamp is a rest, not a trip.** "Travel to the Hunter's Dream?" OK sets flag
  70000100 + map*10000 + lamp (7MMM01LL; 7MMM02LL lights a lamp) for common event 7200, which
  touches the lamp (2004[14], anim 101160) and warps. Talk command 11 (`0x0204232E`, flag in
  r15d) is pointed at a zero flag range for that flag, so it is never set; the player plays
  101160 through ForcePlayAnimation (`0x0132E290`, started 0.5 s later or the closing talk
  cancels it) and rests until walking 4 m away. ForcePlayAnimation's 7725 does nothing.
- **Phantoms get no lamp prompt** (cause not found: the guest's lamp script runs and f52 is
  false). A guest joins a rest by standing within 2.5 m of where a member used the lamp; only a
  rest started at the lamp is joinable.
- **Coordination:** shadnet `POST /bloodborne/party/rest` ("<member> <lamp> [x y z]", party
  header) returns the party's current rests (fresh for 5 s). Each emulator posts once a second
  in a session (`RunRestBoardClient`, own thread, cpp-httplib) and refreshes once on the rising
  edge of "every session player rests at my lamp" (session players: [GlobalState+0x16F8]+0x14).
- **The refresh** (`RefreshWorldAtLamp`): RequestFullRecover + the bonfire regene
  `0x018FAC80(player, 0)` (rest special effects 100-109; RequestFullRecover alone left HP as it
  was), the respawn load's restock from storage `0x014DBB70` then `0x014DACE0` (vials/bullets),
  and the Dark Souls bonfire respawn left in 01.09: WorldChrMan(`0x0553E878`) +0xA6C = 1.
  WorldChrMan freezes all block characters (+0x359 |= 4), respawns blocks (`0x0190E3B0`) and
  WorldChrMan +0xA40 (`0x018EB900`), state 3; SprjEmkSystem (`0x012F0F90`) restarts all event
  scripts and resets temporary flags in state 3, then 4, 5 (thaw), 0.
- What the party rest changes in that respawn: the event restart is skipped (hook `0x012F0FCB`
  moves 3 -> 4); only characters dead now and seen alive during this map visit respawn (hooks on
  the NPC param loads `0x0190E445`/`0x0190E572` hand living ones a stand-in param with the skip
  bit +0x145 & 0x2000000); disabled-alive characters ([+0x18]+0x20 bit 0, set by `0x018C6390`)
  are disabled again; state flags 0x4000/0x20000/0x40000000 of live characters are put back.
- New hook API: `InstallGuestRegisterEditHook` (cpu_patches): the trampoline already reloads
  registers from the snapshot, so a hook can change them.
- `py -3.12 tools\re\esdump.py <talkesdbnd.dcx> [esd]` dumps talk scripts (states, conditions,
  commands). Bloodborne Enhanced's decompiled talk/event scripts (github
  colorsolid/bloodborne-enhanced) are a good map; its Reawaken is a real reload.

## Next (user-approved order)
1. Rest of Phase 3 (travel together, boss kill doesn't dismiss), then Phase 4 (shared progress,
   guests as full players: loot, full HP/vials, their own save progress; they stay white
   phantoms as the network role).
2. The backlog at the end of this file.

## Planned for the end (Kaniel's call)
- **Death refinements** (Kaniel moved these to the end, 2026-10-05):
   - Test with more than 2 players (rig has two; needs a third instance or a friend).
   - World refresh: dying to a boss should refresh the world; decide the general rule for when
     the world refreshes (see lamps as campfires above).
   - Lamp teleport camera: it swoops to the lamp instead of cutting; look for the snap flag of
     the move (vtable +0x5F8's mode argument is 1 now).
   - Lamps in another block/map: currently no teleport (needs a load, which needs the summon
     handshake, see "Travel together").
   - Invaders (black phantoms) dying in seamless mode skip the warp home too; only relevant if
     invasions are kept.
- Near-zero join wait: the host only searches every 5 s (`--summon-poll`; also drives the
  guard timers, so don't set 0). Plan: have shadnet hold `summon_messenger/get` open ~3 s and
  answer as soon as a same-party sign arrives (long-poll). Check first whether the game's
  matching task tolerates a slow answer (it sets a 15 s connect timeout, no receive timeout).
  Fallback: 1 s interval.
- Signaling: fix the lingering-connection reuse above if it shows up again.

## Phase 3: seamless travel (branch `phase3-travel`, 2026-10-06)
Kaniel's design: either player travels on their own, nobody is pulled along or sent home, and the
session stays up across maps. Kaniel chose option A (stretch the native session), see
`docs/SEAMLESS_TRAVEL_PLAN.md` for the full findings and dead ends.
- **Rig-confirmed:** host round trip (lamp to the Dream, headstone back) and guest round trip; both
  see each other and hit enemies afterwards. The guest uses the Dream's NPCs, Doll and pickups.
- **How:** `PartyTrip` (stage transition 0x013CDE30 marks a trip until the new stage is up) skips
  the teardown StopRequest, every LeaveSession and CSMultiPlayMan StopRequest (0x01ED07A0). On
  arrival the join tasks the reload wiped are rebuilt once every member is on the map (shadNet
  party map board, `POST /bloodborne/party/map`): allocate, construct 0x01E4F9F0, add
  0x01E55A70, saved slot/flags, SetStep(4). A guest also rebuilds its own task (kind 2). Bell
  effects (9000/9003, 9005/9006) are put back.
- **Guests use lamps:** talk hooks for ESD f14 (0x0203B7FB) and the talk object's phantom check
  (0x0203AC04). Lamp event 7200 and headstone event 12107000 end for clients, so the emulator does
  a party guest's trip (WarpNextStage_Bonfire; headstone flag 7210MMML -> point MMM2950+L),
  then moves it to the lamp a second later.
- **Party rests are off (WIP, Kaniel's call):** a lamp travels on the first press again;
  `SHADPS4_BLOODBORNE_LAMP_REST=1` turns rests back on.
- **Boss kill keeps the session** (BlockClear2_2/2_3 patches). Not rig-tested yet.
- Guest-as-host experiment (`SHADPS4_BLOODBORNE_GUEST_AS_HOST=1`): full HP bar, but the model is
  invisible and lamps stay hidden. Off by default.
- Diagnostics to trim later: guest talk answers/flags, join-task and member-leave logs.
- Rig launchers have `SHADPS4_BLOODBORNE_RE_TRACE=1` on (Kaniel asked for it).
- Both rig clients log "permission denied" opening `userdata0005`/`0010` on every save; check
  whether saves land.

## Backlog (Kaniel 2026-10-06: "add all of the current findings and problems to the backlog")
- **Window monster after a rest:** a hostile character near the Central Yharnam lamp (only meant
  to appear after a quest) appears the moment a party rest refreshes the world, on fresh saves
  too. Bisected: not the lamp use, not WorldChrMan +0xA40, gone when the respawn is skipped. Not
  explained by the disable bit, the +0x1E0 flags, dead-since-load, the event restart or the
  +0x359 freeze (no character was frozen by the scripts). Logged suspect: handle 0x1001400c,
  alive but inactive (control state 0) before the rest and active after, around
  (-198, -28, 61). Untested suspect: the respawn's per-block call `0x018DAD90`. Alternative:
  a real reload of the whole party at the lamp once "travel together" exists.
- **Breakables** (boxes) are not restored by a rest (objects are outside the chr respawn); Kaniel
  also saw boxes vanish, possibly the rig's low-spec profile. Check without the profile.
- **Both rig clients crashed** during a second rest in a build with the freeze/thaw experiments,
  running without Potatoborne; the experiments are reverted and the re-hide now checks
  pointers against the live world. Watch for it.
- **Living wounded enemies keep their damage** after a rest (only the killed respawn).
- **Phantoms:** guests get no lamp prompt (rest by standing near instead) and see quest
  characters as transparent phantoms; Kaniel wants guests to use lamps and interact with quest
  characters (Phase 4).
- **Full lamp menu** like Bloodborne Enhanced: rest, level up, travel, storage, workshop, shops.
- The travel dialog still says "Travel to the Hunter's Dream?" for a rest.
- Diagnostics to remove once the window monster is solved: `LogCharacterChanges`,
  `LogCharactersActivatedByRest`, the lamp availability and talk-script counters.
- Rig: Potatoborne drops enemy corpses at once (Kaniel saw bodies vanish); without it two
  instances run out of memory more easily. A join once failed until both games restarted
  (claim delivered every 5 s, guest never joined).

## Known loose ends
- shadnet logs `unhandled POST /v1/users/<npid>/feed`. Harmless so far.
- The backend answers `ChairMessRespawnPointNotice` generically.
