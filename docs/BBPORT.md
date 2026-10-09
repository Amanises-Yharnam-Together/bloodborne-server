# Seamless co-op on the native port (Bloodborne PC)

`bbport/` is [Supermedo/bloodborne_pc](https://github.com/Supermedo/bloodborne_pc) (branch
`windows`, v1.6), imported with its history, with
[AdrianCsT/bloodborne_pc](https://github.com/AdrianCsT/bloodborne_pc) 1.6.16 merged in
(deadinside28's 0.4 on Windows and its fixes; both are subtree merges into `bbport/`, so later
releases merge with `git merge -X subtree=bbport <ref>`). Upstream 0.5 is not in yet: its new
modules are Linux-only (local branch `merge-0.5` holds the resolved merge, unported). The game's
own x86-64 code runs natively on a small
C runtime (`bbport/src`) with shadPS4's renderer (`bbport/gpu`). The seamless co-op runs on it
as it does in the emulator, from the same sources: nothing in `emulator/src/core/bloodborne_*`
is copied or forked. All players must run the same build; the port and the emulator play on the
same shadNet server and backend.

## How it is wired

- `bbport/gpu/bbcoop.h` is the C interface the loader and the runtime call; `bbport/gpu/bbcoop/`
  implements it with the emulator's co-op code compiled in unchanged (the `bbnet` library in
  `bbport/gpu/CMakeLists.txt`). The port's own headers (`gpu/shim`, `gpu/shadps4`) come first on
  the include path, `emulator/src` last.
- Hooks: `bbcoop/guest_hooks.cpp` installs them with the emulator's trampoline generator
  (`emulator/src/core/guest_hook.{h,cpp}`, shared with `cpu_patches.cpp`). Trampolines live in a
  1 MiB executable block the loader maps next to the image (`probe.c`, `runtime_low_map`).
- Install: `bbcoop_install` runs after relocation, the community patches and the TCB rewrite,
  before the image is made read-only, so the co-op patches write the eboot directly (image
  offset 0 is the eboot's address 0, as in the emulator).
- Memory checks: `bbcoop/memory.cpp` answers the co-op code's `IsValidMapping` and
  `QueryProtection` with the guest's protections (runtime VMAs), then the image segments, then the
  host's (thread stacks), so the renderer's page tracking never fails a co-op write.
- Seams in the runtime: autoplay in `runtime_pad.c`, the save slot in `runtime_file.c`, shared
  trophies in `runtime_services.c`, the crash report in `probe.c`'s fatal exception handler.
- Network: `bbnet` compiles the emulator's `core/libraries/{network,np}` and `shadnet` (the port
  had a vendored copy of them; it is gone). `BB_ONLINE=1` turns them on; `BB_SHADNET_*` give the
  server and account (`gpu/shim/core/emulator_settings.h`, `user_manager.h`).
- Log: `BB_LOG_FILE` writes shadPS4's log format from `BB_LOG_LEVEL` (default info), which
  `tools/re/check_party_log.py` and the rig read. The build's revision is logged as
  `Run: Revision <hash>`.

## Build

- Windows: MSYS2 CLANG64 with the packages in `bbport/packaging/windows/README.md`, then
  `tools\build-bbport.ps1 [-Src <bbport dir>] [-Test]`: `bash build.sh`, the co-op tests
  (`coop-hooks-test`, `log-file-test`) with `-Test`, and what the rig runs staged in
  `C:\bb\out\bbport` (`run.py`, `scripts`, `patches`, `bin\bb-probe.exe`).
  `tools\ci-check.ps1 -Component bbport` does the same.
- Linux: `bbport/build.sh --test`; needs clang (GCC 13 lacks deducing `this`), Vulkan headers
  with `VK_VALVE_shader_mixed_float_dot_product` (1.4.33x or newer) and miniupnpc 2.2.8 or newer.
  The renderer tests (`ninja -C out/gpu scene-resolution-test taa-shader-test`) run on lavapipe
  (`VK_DRIVER_FILES=.../lvp_icd.json`).

## Co-op kit for players

The port's counterpart of the shadPS4 co-op kit, unzipped into a player's Bloodborne.PC.Online
folder: `SeamlessCoopPort\` and the "... Port Co-op" launchers beside it.

- Build: `bash packaging/windows/package.sh` in the CLANG64 shell (with
  `WINPYTHON=<python.org 3.10+ python.exe>`: it otherwise takes the last Python it finds, which
  may be too old), then `tools\make-port-kit.ps1 [-Package <bbport>\out\stage\bbport-windows]
  [-Servers <shadPS4 kit>]`. It writes `C:\bb\port-kit` and `C:\bb\port-kit.zip`: the port's
  package (the launcher frozen as `BLauncher.exe`, so players need no Python or MSYS2), the kit's scripts from
  `tools\port-kit` (CRLF added), and the servers, backend and map event files of a shadPS4 kit.
- `tools\port-kit\tools\setup.ps1` takes the shadPS4 kit's answers when it sits beside it, copies
  the saves once (from that kit, else the install) to `user\savedata\1`, and makes
  `game\CUSA03173` (the game with its update, links only) and `game\CUSA03173-mods` (the
  install's mods plus the co-op event files), which the port reads beside the game.
- `2 - PLAY.bat` sets the same co-op variables as the shadPS4 kit plus the port's `BB_*` ones and
  runs `BLauncher.exe --run` (run.py). Logs: `user\log\shad_log.txt` and `console.txt`.
- The HOST scripts start the shadPS4 kit's servers when it is beside this one (its accounts).
- Port and shadPS4 players in one party are not tested yet: everyone uses the same kit.

## Rig

- `py -3.12 tools\rig\run_scenario.py --engine port --setup` makes `C:\bb\rig\port`: the game
  with its 1.09 update as one folder (`port\game\CUSA03173`, links only), and `host`, `guest`,
  `guest2` with the emulator rig's baseline saves and party files, low-spec settings
  (`bbport.ini`: 720p, no upscaler, lowest model detail, effects off; 30 FPS) and the rig's
  backend as game server. Each role keeps its own baseline.
- `tools\smoke-boot.ps1 -Engine port` boots one game and checks that every co-op part went in.
- `py -3.12 tools\rig\run_scenario.py --engine port <scenario>...` runs the same scenarios as the
  emulator. The port's console output is kept beside the log (`user\log\console.txt`).
