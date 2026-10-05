# Handoff: state on 2026-10-05

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

## Phase 3 item: bell-less joining (implemented, waiting for Kaniel's play-test)
- RE finding: a rung bell is just special effect 9000 (Beckoning, game adds 9003 too) / 9005 (Small Resonant) on
  the player. The SOS status (`0x0186FE40`) derives "searching" from the effects' special state.
  01.09 adds 9005 itself at `0x01C0D3B0` through the character's add-effect virtual
  (vtable slot `0x3F0` = `0x018C6DB0`), args `(chr, id, chr, 0, 1, 0, 1.0f x5, stack 0)`.
- `StartSummonSearch` (bloodborne_party.cpp) makes that same call. Party auto-rejoin and the
  cross-map responder resume both use it instead of `UseItemNativeApply` (goods 200/205), so no
  item, animation or Insight is involved. The slot is checked against the verified 01.09
  function before calling.
- To verify in the rig: both join without holding bells, the host's Insight does not drop, and
  the log shows `auto-rejoin started search effect=9000|9005 ... result=true`.
- Eboot RE on this PC: scratch scripts flatten `eboot.bin` (fake SELF) and apply relocations,
  then disassemble with Python capstone (`py -3.12 -m pip install capstone`).

## Session teardown (2026-10-05)
- The seamless keep-matching patch (call at `0x019471B1` in the world teardown `0x019470F0`)
  kept the session over *every* teardown, so a guest quitting to the title stayed in the room.
  A hook at `0x01947167` now calls the native `StopRequest` when the guest is summoned
  (state 6) and no summon reload is running. Host-side teardowns (state 3) are unchanged.
- Server: signs not refreshed for 75 s are no longer offered (stale sign from a previous run).
- Known: the emulator's signaling reuses a lingering (locally deactivated) connection when the
  peer re-activates within 60 s; the game never hears that invite.

## Next (user-approved order)
1. **Deaths that don't disconnect (Phase 2):** suppress the phantom-death / boss-clear "return to
   world" teardown.

## Known loose ends
- shadnet logs `unhandled POST /v1/users/<npid>/feed`. Harmless so far.
- The backend answers `ChairMessRespawnPointNotice` generically.
