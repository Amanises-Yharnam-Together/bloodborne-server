# Handover: seamless co-op on the native port (2026-10-09 evening)

Kaniel's direction: the native port (Bloodborne PC, `bbport/`) is the main target; shadPS4 is to
be dropped once the port works well. Port-only solutions are welcome when they make a co-op
mechanic simpler or more reliable. No mixed parties (port and shadPS4 players never meet). Never
leave/rejoin a session as a fix: everything is seamless. See docs/BBPORT.md for how the port and
its co-op are wired, built, packaged and tested.

## The third member's 180 s wait (branch host-search-cooldown, 2026-10-12)

- At 30 FPS the host's searches (/summon_messenger/get) stopped once the second member was in and
  came back exactly 180 s after its claim (rig 20261012-160310, -131858); uncapped, the next
  claim came 55 s after the first (-130950, -130040). Re-ringing the bell did not help.
- Cause: each claimed sign leaves an entry in the summon manager's list at +0x1C0 (updated by
  0x01872360) that takes a summon slot (0x018796C0 counts it next to the session's members)
  until the game sees the summoned player arrive through its own path, which a party join never
  shows it. The entry ages by the frame time (+4) and goes once 55 s old, or 180 s old while its
  +0x0C is set (`vucomiss` at 0x018729D9 / 0x018729ED). Until then the host starts no new
  summon while an entry is pending (0x01876092, the uncapped runs: searching, not claiming),
  and with the first guest counted twice its coop slots can read full (SosStatus 0x0186FE40),
  so it does not even search (0x018723FA, the 30 FPS runs).
- Fix (optional feature "summon slot hold", a configured party only): both compares read a 15.0
  among the game's constants (0x049278C4) instead. Rig check: join3 at 30 FPS, the host keeps
  posting searches every 5 s after the first claim and the third guest is claimed within ~20 s.
  Removing an entry may show the game's own summon notice (message 140050/140058, 0x01872AD2);
  it did so at 180 s before, if at all.

## One table for the event flags (branch flag-table, 2026-10-12)

- Whether an event flag is the host's, each player's own, shared by the party or only for the
  session was decided by a dozen hand-written helpers; tonight's bugs (Insight twice, Dream doors,
  the bell, a boss's "found" Insight) were each a flag in the wrong group. Now one table,
  `FlagTable` / `RuleFor` in `emulator/src/core/bloodborne_flags.h`, holds every rule and the
  helpers read it. Nothing decides differently: the old helpers' answers over ~0.5M flags are
  recorded in `tests/bloodborne/flag_rules_expected.txt` and the table must match them. The table
  in plain words: docs/FLAGS.md. A new flag rule is a new row there.

## Damage straight from game to game, and the stuck third summon (2026-10-11 evening)

- **Direct damage over the P2P link.** Each game's HP board report also goes to every room peer
  over the port's own UDP transport (`p2p_sockets.cpp`: "SHAD" frames of type 0x30 on the
  internal vport, `P2PCoopSendTo` / `P2PCoopRecvFrom`), at once when it changes and every 0.5 s
  (`DirectDamageType`, `EncodeDirectDamage`). The peers are the room's live matching2 signaling
  links (`GetActiveRoomPeers`, a snapshot the handshake thread takes; every member links to every
  other). Frames are read every frame, only from those peers (`TakeDirectDamage`), kept by sender
  for 2 s (`DirectDamageInbox`) and merged with the board's copies of the same reports
  (`MergeEnemyHpReports`: most damage, least HP). A hit (or a frame) runs the enemy step on the
  next frame (`enemy_step_due`). The board stays underneath. Rig hit3 lag from a hit to the other
  bars: 0.52 s (200 ms board) -> 0.33 s (#18) -> 0.07 s.
- **A report ahead of the game's own copy of a hit**: a guest's report reaches the host 0.1 s after
  its hit, before the game's copy; the host wrote it and the game's hit took it again. When the
  game's copy lands after a write, the write is given back (raised again), and that member's
  damage is held `SharedDamageHoldMs` (250 ms) for the game's copy from then on (`game_sends`).
- **The stuck third summon** (rig hit3 twice in a row): after the second guest's join the host's
  searches got the first guest's sign 48 times and never claimed it. The host's summon queue
  (`QueueInsert` 0x01875320: a map at [status+0x10] keyed by the prepared object's first u32,
  entry byte +0x23) already had an entry for that sign's prepared object: the insert overwrote it
  keeping +0x23 and queued nothing ([status+0x78] stays 0, so `RequestQueued` 0x01873605 never
  runs). The QueueInsert trace now logs the key and any existing entry. Server fix: a sign shown
  to one host `UnclaimedOffersBeforeNewId` (4) times without a claim shows a new id, i.e. a new
  pending candidate on the host. Unit-tested; the rig runs after it joined in ~20 s without
  needing it (the stall is intermittent).

## Double damage and duplicate enemies (Kaniel's friend run, 2026-10-11)

- **A guest's hits counted twice.** After a trip each member runs its own copy of the map, but the
  guest's hits still reach the host's copy through the game. The host counted that drop as its
  own damage and posted it to the HP board, so the guest's copy took every hit a second time half
  a second later (friend's log: 0x10014066 450 -> 401 its hit, 401 -> 350 from the board), and
  the host's copy took the guest's post on top of the game's hit. Now a member's own damage is
  what its own hits did: hooks on the game's hit function (`HitOffset` 0x01A2A770, entry: the
  victim at [rdi+8], the attacker in rsi; `HitAppliedOffset` 0x01A2C05F, the AfterHit return:
  HP after) count, per handle, what hits by the local player took off (`own_hit_damage`). The
  rest of a copy's drop (another player's hit the game sent) pays for the others' posts before
  the board writes anything (`SharedDamage`, `EnemyCopy::own_hits`). The first hits of each
  kind log "hit on <handle> by this player / another player / other". Counted hit by hit, a
  world that shows its characters at 1 HP for a moment posts nothing (the friend's world posted
  97 such characters and the host's 97 copies went to 1 HP). Poison and other status effect HP
  ticks (0x01A2FA40, hooked at its two SetHp calls `EffectHpChangeOffsets`, which run only when
  HP changes) count for the member whose own hit was the last on that enemy (`OwnDamage`). The
  rig's `hurt` counts as the player's hits. On the rig the host never logged a guest's hit as
  "another player": the guest's hits reach the host's copy some other way, which counting only
  one's own hits does not need to know. Rig (all pass, 2026-10-11): guest-hit-once,
  hit-after-host-trip, hit3; `hp_sums_hits` checks the census equals max HP less every hit.
- **Bars late for the host's hits** (forest run 2026-10-11, 3 players): the host's hits (285 on
  456 HP woods enemies) do not reach the guests' copies through the game, only its kill does (a
  hit by "other 0x0" taking all HP). Over the HP board the bar dropped about a second after the
  damage number, often after the host's second hit had killed it. The board now posts at once
  when this member's own damage changes and runs on 100 ms rounds (`HpBoardRound`,
  `HpBoardWait`); rig hit3's average lag from a hit to the other bars went 0.47 s -> 0.30 s
  (`max_hp_lag` 0.4).
- **A guest unseen after the host's early trip, without party death.** The host asks a member
  that was still joining for its data only once the vitals board shows it on the host's map
  (`FinishWaitingJoins`), and the vitals board ran only with party death on (the rig runs with it
  off). Now it always runs; without party death a member posts Up, its map and position.
- **Duplicate enemies at a border.** The kill board's visit (`VisitTracker`) ended on every map
  change, and walking from Central Yharnam into the Cathedral Ward is one: the host withdrew all
  72 kills of its visit (still dead in its world) and they came back to life for the guest; the
  guest killed them again for no echoes, and the same for the NPC by the Ward lamp. A visit now
  ends only on a load: a party trip, a reload out of a session, or a summon onto another map; it
  keeps the map it began on.
- Seen in the host's log, not looked into: after the Gascoigne kill the time-of-day reload
  (0x0193C65C) ran several times in a row by the Ward/Central Yharnam border, each a party trip
  flipping the map between 0x18000000 and 0x18010000, and the same by Hemwick (0x16000000).

## Backlog (Kaniel 2026-10-10)

- **Progress-flag item awards for party guests** (common event 9040, 2026-10-13). common.emevd
  event 0 starts 9040 per slot with (flag 9042-9077, item lot 17000-39010): it ends if the flag
  is already on, ends unless the local player is alive (as 9100, the NPC gifts, did), waits for
  the flag and awards the lot. A party guest never runs it. What sets those 90xx flags (bosses,
  story steps) and whether each guest should get the item is not known yet; the NPC gift fix
  (GivesGuestNpcGift) covers event 9100's talk triggers only. Do not patch 9100/9040 in the event
  files with per-player trigger flags: the kept-flag write raced the event's own clear and the
  host was given the gift about 20 times, then crashed (rig iosefka-gift, 2026-10-13).
- **A guest's lethal hit just after its join may not put it down** (rig guest-death, 2026-10-13,
  party death always on). The guest's 1 HP and 25 m rise came 10 s after the session formed, as it
  took the host's world state ("106 flags changed at once (a load)"): it never went down
  (`wait-vital down` timed out), and its real death (reset=false, so armed by then) was logged only
  after the script had failed. Arming (`TickPartyDeath`) needs the player in the party here, a
  fresh vitals board and another member standing on this map. Next: log the armed state, the
  board's age and `InPartyHere` at each lethal catch in that window. The rig's deaths wait 15 s
  after the join, as give-up and down-revive do.
- **Guest unseen after a host's early trip.** A host that travels (lamp, Dream) while a guest is
  still loading in (its join task on the host at step 3, the guest's data not synced, flags 0x4)
  never gets that guest's character data: the data comes once, by slot, and is gone with the
  host's world. Back on the map the host rebuilds the task at its saved step and slot and waits
  (`RebuildsFromSave`, `joins_waiting_for_data`), but the data never comes again, so the guest
  stays invisible to the host until it rejoins. Rig: hp-together with the host's "wait 40"
  before its trip cut to "wait 10" (with the ping) reproduces it.
  **Found and built (branch char-data, not rig-run yet):** how the game sends a member's
  character data and how to ask for it again; the whole map is in `bloodborne_party.h` above
  `MemberDataWait`. In short: each game sends its own data in parts on packet channels 8 (stats,
  face, name), 0x0B, 0x0C (gear), 0x0D (+0x13, 0x3B, 0x3C) after every load; the receiver fills
  its remote slot (GameDataMan +0x10, 0x6A0 each) and a parts byte per slot (+0x18, all = 0x1F).
  A one-byte packet on channel 0x0A is the game's own "send me your data", answered with every
  part. Now the host asks a waiting guest for its data (every 5 s until all parts are in) when
  it is back on the guest's map, then creates its character; and a player whose stats or face
  change in a session (level up) sends everything again (`OwnDataWatch`, the game's after-load
  broadcast 0x01781AC0). Gear changes the game sends by itself. Rig: `early-trip` (host trips 2 s
  after the session forms; then the guest's `raise-stat`).
- **Join stall: the guest connects but never joins** (rig ward-npc 20261010-200656, 1 of 25
  joins): the host summoned, both games' signaling came up, and the guest never removed its sign
  or called JoinRoom; both dropped the link 6 s later. The guest joins on the host's signaling,
  not on the server's claim delivery (holding the claim back did not stall it), so what the
  guest waits on is unknown (earlier note: OnMatchingCheck 0x013808B0 never ran). In since
  then: the server drops a delivered claim nobody joined within 20 s and offers the sign under
  a new SessionId (the host keeps the old one on its pending list, [manager+0x1A8], and never
  summons it again), and the guest logs "a summon came 8 s ago and the game has not joined"
  with its matching and session state. Not seen working yet: no way to force the stall.
- **Guest cannot hit the NPC by the Oedon Chapel lamp** (Kaniel, Cathedral Ward; rig ward-npc,
  NPC 0x10010075): the host hurts it to 600 and both games show 600, but none of the guest's
  hits land in 20 s. Kaniel: guests should be able to damage NPCs.
- **Iosefka still drawn on a guest** (Kaniel 2026-10-10, rig games, new characters): the woman
  upstairs in the clinic (0x10014005, entity 2410710, at -200.0 -40.5 209.8) shows on the guest
  only. Her hide byte (+0x28) is mirrored and set in both worlds; copying the host's two extra
  state flag bits (+0x1E0: 0x8000, 0x1'0000'0000) too did not hide her (tried, reverted). The
  host's story unloads her model: m24_01 events 12410000 and 12410645 run `Set Character
  Backread State` (2004:29) on 2410710 by Iosefka's state flags 1140-1146, and the `dump` verb
  shows her model, sound and texture modules empty on the host and loaded on the guest. Fix:
  find the game's 2004:29 handler (or the backread switch it calls) and run it on a guest for
  characters the host has unloaded; the shown board would carry a "backread removed" bit. Rig:
  `--play iosefka-new` (a copy of Kaniel's new character, rig/saves/new-character), scenarios
  iosefka-look / iosefka-new (pictures, census, dumps).

## Sync fixes and a ping on the rig (branch sync-fixes, 2026-10-10)

Kaniel's playtest on the rig (play-down-revive): the guest saw Iosefka at her clinic door while
the host's world hid her, and on new characters the dog 35 m from the first lamp died by itself
in the host's world, its loot on the ground.
- **A ping on the rig** (default 50-100 ms round trip, `--ping 0` for none): each game holds what
  it sends the others half the ping (`SHADPS4_P2P_DELAY_MS=25-50`, `net_delay.h`, in the P2P
  transport's send; packets keep their order), and the runner puts delaying TCP proxies 1000
  ports above shadNet (32313), its web API (the party boards, 32315) and the backend (20453)
  (`tools/rig/latency.py`). The games derive shadNet's STUN port as the TCP port + 1, so with a
  ping the runner sets the rig's `shadnet.cfg` `MatchingUdpPort` to 33314 (UDP is not proxied:
  shadNet must see each game's own address). The first try without that: no join (endpoint
  unresolved).
- **Iosefka / story NPCs**: a guest's world, rebuilt as the host's client at its join, does not run
  the events that hide story NPCs, so it showed them. The host now posts every character of its
  map with its event disable byte (+0x28) to a new board, `POST /bloodborne/party/shown` (map
  state board format, `EncodeShownCharacters`), and a guest in the session on that map sets its
  copies the same (`ShownLikeTheHost`, skipping those its kill board hid). On the rig the guest
  hid 6 characters the host hides (Iosefka 0x10014005, 0x100140ae, the dog 0x10014009, ...).
  Scenario story-hidden. The rig's shadNet needs this build (`server_is_current` probes /shown);
  so do the kit's.
- **The dog**: before the session the guest's own world posted its hurt dog to the HP board and
  the host's dog was lowered to that (rig hurt-before-join, red: the host's dog at 5 HP before
  the join); Kaniel's host then got +235 echoes and the loot the moment the session formed. Now
  only damage done in the session is posted or applied (`VisitDamage`: none out of a session,
  each enemy counted from its HP when the session or the visit began, or the most since).
- **Invisible guest after the host's trip** (Kaniel, hp-after-host-trip with the ping): the host left for the
  Dream while its join task for the guest was at step 3 (data not synced, flags 0x4); back on the map
  it rebuilt the guest from that save into the character step with the data marked synced: a
  "Beckoner" name plate and no body. Saved tasks now keep their step, and a member still joining is
  rebuilt as the game's own fresh join (`RebuildsFromSave`). Not yet rig-confirmed (Kaniel stopped
  the rig): rerun hp-after-host-trip with the ping and look at the guest on the host's screen.
- **First guest misses the second guest (3 players)** (rig smoke 2026-10-12 on 40ea3277, first
  guest's expect-others-near 2 failed after the host's trip): the first guest's game makes no
  join task for a guest that joins after it; our re-create after that join (`RecreateAfterJoin`)
  built one with no saved tasks yet, and `RebuiltTaskKind` then guessed "the host" (kind 0). Step
  4 (0x01E50090) creates only kinds 1 and 2 (`dec eax; cmp eax,1; ja` skips the creation), so
  the second guest never got a body there; the wrong kind was then saved and rebuilt at every trip.
  The flag 0x40 (+0xED = 0x4c) was only a symptom: the constructor (0x01E4F9F0) keeps bits 6-7 of
  the allocation (`and 0xc0`) and no join code sets or tests them. Now the live join list counts
  too (the host's live kind 0 task makes the new member a guest, kind 1) and rebuilt tasks drop
  bits 6-7 (`RebuiltTaskFlags`). Confirm with rig smoke (or join3/trip3): the first guest's
  "rebuilt the join task of a member kind=1" after the second guest joins, and its
  expect-others-near 2 in the trip chapter.
- Kit 2026-10-10 17:11: C:\bb\port-kit-sync.zip (servers from C:\bb\out\sync-kit-servers = online-kit
  + the new shadNet). The host's HOST bat runs ..\SeamlessCoop\server\shadnet.exe when it is there:
  that copy needs the new shadnet.exe too.
- New rig verbs: `census [<meters>]` (every character that close: HP, death flags, hidden,
  disabled, position) and `expect-hidden <handle> <0|1> [<timeout>]`. New scenarios:
  story-hidden, hurt-before-join, clinic (both players by Iosefka's door, censuses),
  guest-flags-stay (a guest's own flag sets do not reach the host's world: passes).
- Checked and not the cause: the game does not pass a guest's own event flag sets to the host
  (guest-flags-stay). Kaniel's host did get Forbidden Woods flags (12700130...) late in that run,
  9 s after the guest's world set them again; where from is not known yet.

## Harder enemies (branch harder-enemies, 2026-10-10)

Kaniel: bosses are too weak for a party (poise-locked and R1-spammed), so every enemy and boss
gets more HP, damage and poise. `SHADPS4_BLOODBORNE_HARDER_ENEMIES=<hp>,<damage>,<poise>`
(kit SETUP asks, default `3,1.5,3`, `off` for vanilla) scales the loaded tables once, from the
title screen on (`MakeEnemiesHarder`, rules in `bloodborne_party` + tests):
- NpcParam `hp` (+0x20) x hp; 1-HP event dummies stay.
- NpcParam `superArmorDurability` (+0x118, poise) x poise, with a floor of 15 (50 for base HP
  1000+) x poise, since most enemies and bosses (Gascoigne: 0) have none; 9999 stays.
- AtkParam_Npc `atkPhys/Mag/Fire/Thun` (+0x50..0x56) x damage. AtkParam_Pc has the same type
  and is told apart by row 271000 (Gascoigne's first axe swing). Hostile NPC hunters that fight
  with player weapons use AtkParam_Pc and are not scaled yet.
- One HP multiplier for bosses too (Kaniel): the game's co-op boss HP bonus (SpEffect
  7500-7503 maxHpRate x1.5-x3) is set to 1 and KeepBossHpInStep stands down; the rows' status
  resistance bonus stays.
- Shared enemy HP is synced as a share of max HP, so a member with another setting stays in step,
  but everyone should use the same setting. Not checked yet: whether poise > 0 really stops the
  stagger on every hit for enemies that had 0 (rig + playtest).
Offsets from Paramdex BB defs, checked against the 01.09 gameparam (row size 0x184 / 0xA4,
Gascoigne 1440 HP, poise 0, first swing 136 physical).

## State 2026-10-10 early morning

Rig on 3f730dc0 and later: guest-crash, host-crash, resume, crash-apart, boss-kill,
host-death-boss, join3, crash3, guest-trip3, trip3 (after the fix below), crash-rejoin-hp,
guest-talk-at-lamp, guest-talk-by-the-lamp, pickup, pickup-after-crash, hp-across-blocks,
guest-death-seen and echoes-after-crash pass.

- Fixed: a guest that went to the Dream lost the slot of the guest it left behind (its trip
  back saved only the tasks found in the Dream), so back on that guest's map the rebuilt task
  had no slot and the two guests never saw each other (trip3). MergeSavedJoinTasks keeps the
  saved tasks of members not met on this map.
- crash-rejoin-hp checks only the host's hit: in a session the host's game owns its enemies' HP
  and puts a guest's direct write back within seconds (the rig's hurt on a guest is no hit).
- New rig verb `pickup [<meters>] [<timeout>]` (onto the nearest usable map item, X until it
  is gone); `teleport` and `approach` take a heading; snapshots give nearby positions. Never
  pick up far items: a jump 21 m down from the lamp item drops the player through the floor
  and crashes the game in 0x0191B6E0 (a character lookup by entity over a block's list), with
  or without a crash before (pickup-far, removed).
- Forest pickups (Kaniel's log, 2026-10-09): the item button kept finding items but no press
  ever reached the pickup (0x01CD91F0, the action button check, said no) from the time-of-day
  reload into the Forbidden Woods (a trip that kept the session) on, before the friend's crash.
  Not reproduced: pickup-after-crash, pickup-after-trip (both to the Dream and back) and
  pickup-after-reload (in-place warps) all pick up. Next: a real time-of-day reload, or the
  item there (0x52 by the start of the woods carried flags 0x41, the others 0xf841).
- Fixed: a guest talking to Gilbert at his window by the Central Yharnam lamp (talk 241319, 3 m
  from lamp 241001) went to the Dream. His talk sets 70000140, which IsLampTravelFlag took for a
  lamp's trip (any 7MMM01LL but the Dream's); now only map codes 211-399 count (the 799001xx
  many NPC talks set are out too). Rig guest-talk-gilbert reproduced it (heading h faces
  (-sin h, cos h)).
- Join stalls (no session for minutes, both sides waiting): 4 of 19 joins from 01:12 to 02:52 on
  2026-10-10, against about 1 in 60 before; 7 of 7 again once the per-frame talk-position hook
  (0x0204C63B, every talk every frame) and the guest's per-frame talk-start log came out (kit
  3bc22a7f still had them). Two shapes: the guest got the claim but never ran the host's
  matching check (OnMatchingCheck 0x013808B0), so it never removed its sign
  (SummonDataRemoveRequest) or called JoinRoom; or the host made its room but its queued summon
  never started (SummonSelection.QueueInsert without RequestQueued 0x01873605, which needs
  MultiPlayMan +0x124 == 3, [rbx+0x78] and [rbx+0x90] == -1). After a stall the host keeps the
  sign in its pending candidates (list at [manager+0x1A8], entry +0x10 state) and never asks
  again. A guest re-ring did not help (the sign keeps its SessionId) and was taken out again.
- Enemy HP after trips (Kaniel 2026-10-10, "not shared after tp's, I think"): hp-after-host-trip
  and hp-after-guest-trip pass, the game's own sync both ways within 2 s after either trip
  (the guest's real hit via the new rig verb `hit`). The boards cover members apart: lowest HP
  wins there, so two members hitting their own copies apart do not add up. A first failure was
  the test's timing; step lines now carry t= (one clock for both games).
- A host's warp to another lamp of its own map (pickup-after-reload, the first try of
  guest-talk-gilbert) makes the guest's game drop the room for a moment; players cannot do that
  (lamps lead to the Dream), so the scenarios no longer do it.
- lamp-after-crash fails: a lamp the guest lit in the session reads unlit in the host's world
  after the guest's crash and rejoin (its own world has it: the progress file sets it at
  load). The flag board gives a new game nothing older than its first post (a new character
  must not get old lamps), and the host posted that lamp before the crash.

## State 2026-10-09 night (branch merge-05b, to become bbport)

Done (Tasks 2-6, merge first): deadinside28 0.5-pre3 merged onto the AdrianCsT base (0.5's
settings pages in the game's System menu with Adrian's XeSS/frame generation; co-op keeps the
PLAY ONLINE/OFFLINE screen, 0.5 skips it into offline otherwise). The 0.5 PC memory model runs
on Windows (host import of the runtime's section views, game code hooks with System V ABI;
opt-in BB_PC_MODEL=1; free_check/gnm_hooks diagnostics still stubbed). Fixes: staging pool lock
(the friend's RTX 3070 crash dump: RecordHostCopy -> RunGuestCopy -> Buffer::Flush into a
dangling staging buffer), enemy HP shared across blocks of one area (the guest rejoined in place
on another block), item-button and remote-death diagnostics on in every co-op game. Tried and
reverted (both broke joins in the rig): 1 s sign polling (the guest got a claim every second
and never joined the room) and skipping the game's 5 s arrival wait at 0x01E4B09F (the guest
dropped the host's connection ~5 s after it formed: lone-guest, host-crash). Rig tests run in
the background now (no focus taken). Full run 2026-10-09 (stopped at Kaniel's word): 15 passed
before the two failures; lone-guest and host-crash pass again without the patch; the rest of
the set (resume onward, the new scenarios, join3/trip3/crash3, soak) is still to run. Save tab in the settings menu (BbSaveEditor: echoes, insight, attributes through
0x1c13d00, items through 0x131cb70 on the game's thread from its pad reads; item list from the
shadPS4 kit's DATA/Bloodborne Save Editor/resources). New rig verb expect-others-near and
scenarios hp-across-blocks, echoes-after-crash, lamp-after-crash, crash3, host-death-boss, soak.

Open: invisible guest after respawn and host pickups after a guest crash have no cause yet
(the new diagnostics and guest-death-seen should show it); a fixed ~6 s post-load phase before
events start (paid twice per join, not found yet); the save editor's offsets were read from the
eboot, check the "Save editor: level ..." console line once in the world.


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
