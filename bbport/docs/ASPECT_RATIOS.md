# Any resolution, any aspect ratio

Kaniel 2026-10-24: the port runs at every resolution the monitor has, ultrawide 21:9 and 32:9,
16:10 and 4:3 included. A wider screen shows more of the world to the sides, a taller one more
above and below; the picture is not stretched and has no bars.

The camera, HUD and UI parts are a port of deadinside28's bbport 0.6 ("any aspect ratio, live,
through hooks", [deadinside28/bloodborne_pc](https://github.com/deadinside28/bloodborne_pc)
8185b88, GPL-2.0-or-later, the same lineage as this port). Thanks to deadinside28.

## Settings

| bbport.ini | Values | Menus |
| --- | --- | --- |
| `output_res` | any `WxH` (old values still work) | Insert menu > Display, the game's System > Display page, the launcher |
| `aspect_ratio` | `auto` (default) or `16:9` | the same |

- The lists are the monitor's own modes (SDL in the game, `EnumDisplaySettingsW` in the launcher),
  each size once, from 1024x576 to 7680x4320, plus the desktop size and the 16:9 sizes that fit
  it (720p, 1080p, 1440p, 4K) for windowed play.
- Before an output is chosen it is the game's 1080 lines in the desktop's shape: 2560x1080 on a
  21:9 monitor, 1728x1080 on 16:10, 1920x1080 on 16:9 (as heavy to draw as the game's own size).
- `auto`: the output's own shape. `16:9`: the old picture, the largest 16:9 inside the output,
  with black bars.
- Windowed: the window takes the output's size (maximized when it does not fit). Borderless: the
  desktop's shape (a choice of another shape is refitted and saved, so the next start's resolution
  patch has it). Exclusive fullscreen: the display switches to the chosen mode at the desktop's
  refresh rate.
- Live when the session is not fixed by the startup resolution patch (`live_resolution`); with
  the patch a change applies after a restart, as the output resolution always did.
- Upscalers (FSR 3/4, DLSS, XeSS, TAA) and frame generation take the output: the scene renders at
  output / preset scale, even sizes.

## How it works

The game keeps its 1920x1080 frame; the port follows the output in the game's own code:

- **Camera** (`BbGuestHooks`, camera parameter stores 0x1436c2c, 0x1436d8a, 0x1436e68,
  0x1436eec, 0x1436f4b): after the camera manager copies the projection parameters, the output's
  aspect ratio replaces 16:9. A taller output keeps the width and widens the vertical field of view
  (`2 atan(tan(fov / 2) * 16:9 / aspect)`).
- **Markers over characters** (lock-on, health bars, damage numbers: 0x1644379, 0x1644c45,
  0x16452b7): their placement constant is scaled, so they land on the character.
- **The UI** (Scaleform `MovieImpl::SetViewport` 0x1f510): the game's full-screen movies get the
  output's pixel aspect ratio and lay out their 16:9 screen in the middle of the wider (taller)
  view; the UI composition stretches the 1920x1080 frame back to the output.
- **HUD at the edges** (the in-game screen update 0x1bf9e10): the HUD's groups move toward the
  output's edges through the movie's own members (`PlayerHUD` left and up, blood echoes and insight
  right, the boss bar down), and so do the party HUD's portraits, icons and supplies; the co-op
  core places the party list's tags beside the player's bars (`PartyListPosition`).
  `BB_HUD_ANCHOR=0` keeps the HUD in the middle.
- **Full-screen fills** (menu dimming, fades): the vertex shaders move vertices in the corners of the
  game's 16:9 screen to the output's corners (`EmitUiEdgeStretch`, push constant `motion_param`).
- **Cinematics**: a scene finished without UI is completed at the output's size too.
- The port's own overlays (party list, downed screen, prompts, compass, skip vote) place themselves
  on the game's 16:9 screen in the frame, the HUD-bound ones moved with the HUD
  (`BbPartyLayout::Stage`, `HudX`/`HudY`); full-screen screens (map, team) stay a centred panel.

Every hook checks the game's bytes first (another game version: not installed, and the output keeps
16:9). `BB_ASPECT_HOOKS=0` leaves them all out. Detours go in over an int3 first, as the game may be
running that code.

Tests: `tests/test_output_sizes.cpp`, `test_settings.cpp`, `test_guest_hooks.cpp` (camera, field of
view, markers, viewport, HUD shift on a fake image), `test_ui_composition.cpp`, `test_party_layout.cpp`,
`test_patches.py`, `test_launcher_outputs.py`, and the emulator's party HUD test.
