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
  broker, `PartyController` auto-rings the role bell (host Beckoning goods 200 / effect 9001; guest
  Small Resonant 205 / 9005) from the SOS-status hook, backend `SyncCharaId`, 5 s summon polling
  and co-op guard timers, stale-sign supersede and claim reset after join. User-confirmed:
  rejoin after a death is near-instant.

## Next (user-approved order)
1. **Bell-less joining (Phase 3 item):** start the summon search state that effects 9001/9005
   enable, without the use-item path (no animation, item or Insight). Start from the goods-use /
   effect trace sites in `bloodborne_re.cpp` and the RE capture jsonl. Verify that goods 200 is
   the Beckoning Bell from the capture.
2. **Deaths that don't disconnect (Phase 2):** suppress the phantom-death / boss-clear "return to
   world" teardown.

## Known loose ends
- shadnet logs `unhandled POST /v1/users/<npid>/feed`. Harmless so far.
- The backend answers `ChairMessRespawnPointNotice` generically.
