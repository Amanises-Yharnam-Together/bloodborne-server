# Handoff: state on 2026-10-05 (after Phase 2)

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

## Next (user-approved order)
1. **Lamps as campfires** (Kaniel): resting at a lamp is what refreshes the world, and it only
   activates when every party member sits near it.
2. Rest of Phase 3 (travel together, boss kill doesn't dismiss), then Phase 4 (shared progress,
   guests as full players: loot, full HP/vials, their own save progress; they stay white
   phantoms as the network role).

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

## Known loose ends
- shadnet logs `unhandled POST /v1/users/<npid>/feed`. Harmless so far.
- The backend answers `ChairMessRespawnPointNotice` generically.
