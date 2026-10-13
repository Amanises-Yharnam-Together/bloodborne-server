# Agent D: native title menu / co-op lobby reuse (static RE, eboot_rel.img addresses)

## 0. Headline
- The `FrpgMenuDlg*` classes (Title, PlayableTitle, ProfileSelect, KickList, General, YesNoMsgbox, SimpleInfo,
  InfoMultiLine, Logo) are **dead Demon's-Souls-era code**: their only refs are DLRuntimeClass static registrations
  (e.g. FrpgMenuDlgTitle reg 0x016B5AB0, ctor 0x016B6560 only in factory slot 0x05329C10), they were created by
  name from `menu:/menu.drb`, and no .drb exists in the game. Do not build on them.
- The live menus are the **Scaleform "SprjScaleform" dialog system** (same one our HUD bridge drives): movies
  `menu:/%s.gfx` / `menu:/%s/%s.gfx` (strings 0x049B48CC / 0x049B48AC), each opened by
  `OpenDialog 0x01C1CDF0(out job, owner, layer 7, L"<movie>", factory functor)`; the factory allocates a C++ dialog
  and its ctor looks up child clips with **0x01ADB220** (our HudChildLookupOffset).
- Menu flow is a **job/coroutine framework** of ref-counted jobs: `0x0195DB60(out, functor, mode)` wraps a lambda,
  `0x01C1A070(out, a, b)` = sequence, `0x0195DD70(out, cond, job)` = conditional, `0x01B4D2A0(out, bool)` = cond.
  Lambdas are libc++-style functors `{vtable, capture}`; **vtable slot 2 (+0x10) = invoke**.

## 1. Title menu
- TitleStep state table at 0x05573E48 (16 bytes per state, state index at step+0x0C): 0 0x0195A260,
  1 0x0195AAE0, **2 0x0195AE80 = run the title menu**, 3 0x0195B3A0, 4 0x0195B3E0 (CSMovie), **5 0x0195B440 =
  first-time start** (map "24_01_00_00", -> 6), **6 0x0195B480 = load + network (SprjNetworkClientMan)**,
  7 0x0195B6A0 (RapidReentry), 8 0x0195B840, 9 0x0195B920 (EventFlag/Tendency), 11 0x0195BA80.
- State 2 builds: `0x01BB58A0` (opens movie L"Title" with factory 0x053436A0 -> 0x01BDA530 -> ctor 0x01B384F0;
  L"Title_DLC"/"Title_DLC_EU" variants) then a done-functor vtable 0x05330E20 whose invoke **0x0195CB70** is the
  single hand-off: `if [0x05580334]==1 -> step+0xC=5 else step+0x80=[0x05556678]+0x15E0 (save's map), step+0xC=6`.
  0x05580334 ("title result") is written by the start jobs (0x01B3D140 sets 1/2 from GameMan+0x94, 0x01B3E69A,
  0x01B3F3E0) and polled by 0x01B4A090 / 0x01B4B420 (title closes when != 0).
- Title dialog (movie title.gfx): vtable 0x0539AE90 (slot 3 = per-frame update 0x01B39480), 0xC80 bytes. Clip
  PathRefs: +0xA78 "Top", +0xAD8 "PressStart", +0xB38 "Info", +0xB98 "ProgressInfo", +0xBF8 "CommandList";
  0x01B38820 sets OnlineMode / MultiPlay / AppVersion / **ServerVersion** text (element interface slot **0x138 =
  SetText UTF-16(data, wchar*, html=1)**; our code uses 0x130 = UTF-8 variant). title.gfx CommandList has **5 row
  clips Item_0_0..Item_4_0** (row naming fmt "Item_%d_%d" 0x049332EC, fn 0x01AD13C0) + Cursor.
- Press-X sub-menu: 0x01B39030 builds Play Online (FMG 401011, cb vtable 0x0533BE80) / Play Offline (401010,
  0x0533BE30).
- **Main menu builder: 0x01B4A3D0** (functor invoke, title dialog = [rsi+8]). Per item:
  `label = 0x01AE8CC0(out DLString, 200 /*MenuText*/, id); help = 0x01AE8CC0(out, 201 /*LineHelp*/, id);
   list = 0x01B4C9A0(list model, &label (help follows at +0x40), &functor, 0)` (returns model, chainable).
  Items (FMG 200 id, callback functor vtable, invoke = slot 2):
  | item | FMG | functor vt | invoke | condition |
  |---|---|---|---|---|
  | Continue | 401301 | 0x0533BCF0 | 0x01B481D0 | saves exist and last slot (GameMan+0x50 -> +0x7D0) valid |
  | Load Game | 401302 | 0x0533BCA0 | 0x01B479F0 | saves exist |
  | New Game | 401303 | 0x0533BC50 / 0x0533BD40 | 0x01B47860 / 0x01B48B90 | with / without saves |
  | System | 401304 | 0x0533BC00 | 0x01B46DF0 | always |
  | Log In | 401306 | 0x0533BB10 | 0x01B46790 | [0x0558037E]==1 and 0x01AE9AB0() |
  | Information | 401305 | 0x0533B9F0 | 0x01B46320 | via 0x01B462A0/0x01B4E2A0 |
  | Opinion Box | 401307 | 0x0533B950 | 0x01B46030 (-> lambda 0x01B461F0 -> 0x019D42B0 web) | **0x01AE9AC0()**: [0x05578A38]>0 and NetworkClientMan(0x05540288)+0x18 has a URL at +0x50 (dormant on our server) |
  Cancel handler 0x0533B900 (0x0195DB60 mode at 0x01B4AD7E); model bound to child "CommandList" by
  **0x01BEA820** (0x01B4ADB3) and pushed with 0x01C1A070.
- Continue (0x01B481D0): install-check msg boxes, then **0x01B3BFE0(out, title dlg, slot)** = the load/start job:
  `0x01C2CC10` (job vt 0x05346950, functor 0x05346980 capturing dialog+slot) followed by functor 0x0533ADE0 whose
  invoke 0x01B3D140 sets 0x05580334. Load Game's profile-select path (0x01B3F9D0) also uses 0x01B3BFE0.

### Hook points to add "Co-op Party" and to hold the start
1. **Cheapest item**: revive the dormant Opinion Box row: make 0x01AE9AC0 return 1, give FMG 200/201 id 401307 our
   label ("Co-op Party") (msgbnd edit, or a hook on the MsgRepository lookup 0xF6F340(repo,0,cat,id) used by
   0x01AE8CC0), and redirect its invoke 0x01B46030 (vtable 0x0533B950 slot 2) to our lobby. Alternative: a hook just
   before the bind at 0x01B4AD45 that calls 0x01AE8CC0 x2 + 0x01B4C9A0 with our own functor (needs a fake vtable
   in guest memory: 8 slots, slot 2 = our invoke).
2. **Hold the start until the lobby says go**: either (a) hook 0x01B3BFE0 (all Continue/Load starts) and sequence
   a "wait for lobby" job before its result, or (b) hold TitleStep: in the done invoke 0x0195CB70 (or at state 6
   entry 0x0195B480) don't advance until go. (a) keeps the title menu on screen; (b) is simpler but the menu has
   already closed.

## 2. Native widgets for a roster / choice
- **Message boxes** (movie messagebox, Text_0..10 + Item_0_0/0_1 buttons): built by `0x01C28C90(builder, owner, text
  DLString, &type)` + `0x01C28DF0(builder, button label, idx, &{value, 1, name})` + `0x01C28F50(out job, builder)`.
  Ready wrappers (rdi=out job, rsi=owner = dialog+0x48, rdx=text DLString): **0x01C1F500 OK**, **0x01C1EB40
  OK(100)/CANCEL(200)**, **0x01C1ED10 YES(100)/NO(200)** (button labels FMG 204 ids 1-4). Text is a plain
  UTF-16 DLString, so raw text works (build the DLString ourselves or via 0x01AE8CC0 + placeholder replace
  0x01AE7EC0(out, src, tag, value); FMG text tags "<?string?>" 0x0499873E (fn 0x01AE9570), "<?hostName?>",
  "<?joinName?>" exist).
- **Kick list** (Silencing Blank): KickoutTop (0x01BBC3A0) -> L"kickout" opened by 0x01BC2A50 with factory
  0x05340F90 (invoke 0x01BC3E20): a generic ItemList dialog (vtable 0x0539A880, 0xF30 bytes, rows via data functor
  0x05340FE0, select 0x05341210). Rows come from a static DLFixedVector **0x05580E98** (count/selection 0x05580F38)
  filled from the session by 0x01BC1C90. kickout.gfx shows 4 rows (Item_0_0..3_0) + StaticText_302000. Usable as a
  roster by filling 0x05580E98 and replacing the select functor; the row record layout is not decoded yet.
- **Command list** (movie commandList: 5 rows + ScrollBarV/ArrowUp/Down) is the generic scrolling list behind
  System etc.; same model API (0x01BE8A30 new model, 0x01B4C9A0 add item, 0x01BEA820 bind).

## 3. Text input
- Imports sceImeDialogInit/GetStatus/GetResult/Term/Abort (PLT 0x02BC0258 ...). Only user: Scaleform's PS4 IME
  adapter (0x02903F80 opens, 0x02903DF0 polls; vtable 0x052E3FD0; installed at Scaleform init 0x01FCD980 ->
  0x028FD760). So a Scaleform *input* TextField in a movie pops the system keyboard; the game itself uses it for
  the Chalice Glyph search (FMG 267001 "Search using Chalice Glyph: <?string?>"). The port already implements
  sceImeDialog (bbport/src/runtime_services.c:212, keyboard typed into the game window). Party name: feasible via
  an input text field in a patched movie, or by calling sceImeDialogInit directly from our code (simplest).

## 4. Movies (dvdroot_ps4/menu, uncompressed GFX v11)
title.gfx (+title_dlc*.gfx, title.tpf.dcx), titleinformation.gfx, profileselect.gfx, systemdialog.gfx,
messagebox.gfx, ingamemessagebox.gfx, commandlist.gfx, kickout.gfx, kickouttop.gfx, talkchoice1-3.gfx,
quickmatchalter.gfx, summonmessage.gfx, fe.gfx (HUD). Static text fields are named StaticText_<FMG id> (filled
from the text tables automatically; hud_patch.py renames them to stop that).

## 5. Extending the HUD bridge to a menu
Same machinery: dialogs hold PathRefs from 0x01ADB220 (out PathRef, parent, printf name); element interface
slots 0x130 SetText UTF-8 / 0x138 SetText UTF-16 / 0x158 GotoAndPlay; the per-frame hook is the dialog's vtable
slot 3 (title: 0x01B39480) like HudTicksTagsDone. A lobby can be (i) the title dialog's own clips (ServerVersion /
Info / ProgressInfo lines for status text, a patched title.gfx with extra clips like hud_patch.py does for fe.gfx),
plus (ii) native message boxes (0x01C1ED10 yes/no for invites) pushed as jobs, plus (iii) the command-list model for
a menu of online players (each row a functor). PathRef lifetime rule from bloodborne_party.h (release linked values
before the movie dies) applies equally to menu movies.

## Unknown / to verify in the rig
- Whether title.gfx's CommandList scrolls past 5 rows (Continue+Load+New+System+Information already = 5; Log In
  makes 6 when offline, so probably yes).
- Exact DLString (0x40-byte) layout: data at +0x10 inline when capacity(+0x28) < 8, else pointer; +0x20 length;
  +0x00 cached c_str?; allocator field unknown. Need it to build raw strings without 0x01AE8CC0.
- 0x01B4C9A0's 4th arg (0) meaning (enabled/grayed?); how a functor's invoke gets its args (job context).
- Kick list row struct in 0x05580E98 and how the ItemList dialog formats a row.
- Value of 0x0558037E (online-mode flag) on our port.
