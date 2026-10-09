# Handover: seamless co-op on the native port (2026-10-09 evening)

Kaniel's direction: the native port (Bloodborne PC, `bbport/`) is the main target; shadPS4 is to
be dropped once the port works well. Port-only solutions are welcome when they make a co-op
mechanic simpler or more reliable. No mixed parties (port and shadPS4 players never meet). Never
leave/rejoin a session as a fix: everything is seamless. See docs/BBPORT.md for how the port and
its co-op are wired, built, packaged and tested.

## Where things are

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
