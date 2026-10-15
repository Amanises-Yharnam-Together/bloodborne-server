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
  with `VK_VALVE_shader_mixed_float_dot_product` (1.4.33x or newer).
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
  The backend runs inside `BLauncher.exe --script` (package.sh freezes the standard modules it
  needs), so the host needs no Python; its log is `user\log\backend.txt`, and
  `HOST - Stop Servers` stops it and shadnet.
- Port and shadPS4 players are never in one party (Kaniel): everyone uses the same kit. Since
  phase 6 the port plays only in teams made on its team screen, all of its co-op traffic through
  the server's UDP port 31314; shadPS4 keeps the party settings, bells and P2P.

## Public release

`tools\make-release.ps1 -Version x.y.z [-Publish]` (see the README) makes the zip for GitHub's
releases page: the same package and kit scripts, but for players who have nothing but their own
game. Its own files are in `tools\release\kit`: `tools\setup.ps1` takes a game folder (or finds
the one the launcher's Install from PKG made in `game\`), merges a separate 1.09 update folder
over it as links in `coop-game\CUSA03173`, and makes the co-op event files from the player's own
event files (`tools\party_boss_events.py` inside `BLauncher.exe`), comparing them with the hashes
in `event-reference.json`. `tools\release\check_release.py` refuses a release with game files,
saves, logs or settings in it.

## Window mode and HDR output

Port-only display settings in bbport.ini, shown on the game's System menu page "Screen" and in
the Insert menu (the launcher's Window card edits the same keys):

- `window_mode=windowed|borderless|fullscreen`: borderless is SDL 3's fullscreen at the desktop's
  mode, `fullscreen` asks for the closest exclusive display mode. Changes apply at once. Without the
  key the launcher's old `BB_FULLSCREEN=1` means borderless.
- `hdr=1`, `hdr_peak_nits`, `hdr_paper_white`, `hdr_highlights` (0..1), `hdr_scene`: HDR10 (Rec.2020, ST.2084)
  output. Offered only when the Vulkan surface lists HDR10 and SDL reports HDR on for the window's
  display (Windows HDR on); `BB_HDR=1` skips the display check, `BB_HDR=0` turns HDR off. Every
  pass up to presentation stays SDR (upscalers, frame generation, the rig's pictures); the
  presenter composes into an SDR image, draws the overlay into its own layer and encodes both into
  the swapchain (`host_passes/hdr_pass.h`, `host_shaders/hdr_output.frag`): SDR white at the paper
  white, highlights brightened toward the peak.
- `hdr_highlights` (0..1, `host_shaders/hdr_highlight.glsl`): above a knee the SDR picture's
  luminance is raised smoothly; 0.5 takes SDR white to the peak with the knee at 0.4 linear, higher
  values lower the knee, 0 keeps the SDR picture.
- `hdr_scene=1` (default): a gain map per frame (`vk_scene_hdr.h`). The upscaler hooks copy the
  scene color at the post-processing combine (cs 0x9a9cf8a9) and the finished scene before the
  first Scaleform draw; `hdr_exposure.comp` measures the tonemap's mid-tone ratio (SDR / scene
  luminance, smoothed) and `hdr_gain.comp` writes the gain (RG16F, display buffer size): the
  expansion of the UI-free scene, or the scene's luminance at that exposure over the SDR luminance
  where larger, 1 where the UI was drawn (the frame differs clearly from the UI-free copy; small
  differences come from the game's brightness curve in the display pass). Measured in the game
  (2026-10-11): Bloodborne's 16-bit scene at the combine holds little above SDR white (scene / SDR
  at the brightest 1% is 0.5-1.5, a few flame pixels more), so the expansion does most of the work.
  Frames without a scene (menus, movies) and `hdr_scene=0` expand the whole SDR frame.
  `BB_HDR_SCENE_TEST=1` makes the gain maps with SDR output too; `BB_FRAME_DUMP_TRIGGER` then saves
  them as `hdr_gain_NNN_<w>x<h>.raw` (`tools/dump_view.py` draws them). HDR output stays on through
  short drops of the display's HDR state (90 presents). Background:
  /mnt/project-files/research/hdr-feasibility.md.

## Rig

- `py -3.12 tools\rig\run_scenario.py --engine port --setup` makes `C:\bb\rig\port`: the game
  with its 1.09 update as one folder (`port\game\CUSA03173`, links only), and `host`, `guest`,
  `guest2` with the emulator rig's baseline saves and party files, low-spec settings
  (`bbport.ini`: 720p, no upscaler, lowest model detail, effects off; 30 FPS) and the rig's
  backend as game server. Each role keeps its own baseline.
- `tools\smoke-boot.ps1 -Engine port` boots one game and checks that every co-op part went in.
- `py -3.12 tools\rig\run_scenario.py --engine port <scenario>...` runs the same scenarios as the
  emulator. The port's console output is kept beside the log (`user\log\console.txt`).
