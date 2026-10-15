# World sync: where players are, and one ordered log of world changes

Plan agreed with Kaniel on 2026-10-12, from the robustness review. It replaces the separate
per-feature boards and per-feature "which map am I on" checks with two pieces: a place tracker
for each player and a host-ordered log of world changes. It is built in stages, and each stage
ships on its own and is checked on the rig before the next one starts.

## Why

Most hard bugs came from three sources:

- **Map borders, teleports and cutscenes.** 53 places in `bloodborne_re.cpp` each read the
  current map themselves (`GetCurrentPackedMap`), and get 0 while it is not known. At a fog
  border, during a load or in a cutscene, features briefly disagree about where the player is.
  Examples: a border walk withdrew 72 kills, the Ward/Yharnam time-of-day reload ping-pong, and
  guests landing at the world origin after a lamp trip.
- **State surviving the event that should reset it.** Examples: held kills after a wipe, damage
  from before a wipe applied after it, and a member's kill applied again during a death
  animation.
- **Each board having its own rules.** Kills, HP, flags, shown state and map state each have
  their own merge, expiry and reset rules, and a fix to one does not reach the others.

## Stage A: place tracker (one per game)

`PlaceTracker` is a pure state machine in `bloodborne_party.{h,cpp}`. It is fed once per game
tick by `bloodborne_re.cpp` with raw readings, and it is the only thing that decides where this
player is.

- **Inputs per tick:** time (ms); the raw map, 0 when unreadable; whether the player character
  exists; position; whether a load or trip is running (`PartyTripActive`, the load screen);
  whether a cutscene is playing (a game signal still to be found, see below); and a death-reload
  marker.
- **States:** `Unknown` (nothing read yet), `Loading`, `Cutscene`, `Playing`.
- **Place:** `{state, map, visit, since_ms}`. `map` is the last committed map and never turns 0
  because of a failed read. `visit` counts committed arrivals and only ever grows.
- **Transitions with causes:** `Load`, `Border`, `Lamp`/`Teleport`, `DeathReload`, `Cutscene`,
  `CutsceneEnd`. Each one logs a single line:
  `Co-op place: <from> -> <to> map <name/hex> visit N (<cause>)`.
- **Border hysteresis:** while `Playing`, a raw map change with no load starts a pending border
  crossing. It commits only after the new map has read steadily for 1.5 s. Going back within
  that window cancels it.
- **Unknown readings:** a 0 or unreadable map keeps the committed place. A load that ends on the
  same map still counts as a new visit, because the game respawned enemies.
- **Callers:** every feature reads `CurrentPlace()` instead of the raw map, and reacts to
  transitions (`OnPlaceChange`) instead of comparing maps on its own. Features that today pause
  in loads keep doing so through `state != Playing`.
- **Cutscene signal:** still to be found in the eboot, for example the event system's
  movie/cutscene-playing state. Until it is found the state is never entered, so there is no
  behaviour change. During a cutscene, sync changes that would hide, kill or move characters are
  queued and applied when it ends. Looked at so far (2026-10-12): the event command dispatcher
  0x017B93A0 (banks 2000-2013 through the table at 0x017B9544; bank 2004 is 0x017BA6A0), bank
  2002's handler 0x017C53B0 and its PlayCutscene call 0x0131AD70, which only builds and queues
  a request. The state that says a cutscene is playing lives past that queue and is not found.
- **Visit stamps:** every P2P frame and board post carries the sender's `(session epoch,
  visit)`. The session epoch is bumped on a party wipe, a leave, a new session or a new
  character. A receiver drops anything stamped with an older epoch, and for map-scoped data
  anything from an older visit of that member on that map. Today's reset flags
  (`party_kills_reset`, `party_hp_reset`, …) become one dispatcher, `OnLifecycle(Wipe | Trip |
  Leave | NewCharacter)`, where every per-visit container registers its reset.
  Built so far: the dispatcher (`LifecycleResets`, with `Join` for a new session, and
  `MemberLeft` for another member leaving a session that goes on, which keeps the epoch: the
  session member list shrinking for 1 s, `MemberDepartures`) and the epoch.
  The stamps are not: older builds read a damage frame (0x30) as whole 12-byte enemy entries
  (added bytes fail it or read as an enemy) and take a kills frame (0x32) only at its exact
  length, so fields added there break them.
  A frame type of its own (0x34, the sender's place) would not: older builds drop unknown
  types. A sender's frames also stop for its whole load, so DirectDamageLifeMs (2 s) already
  ends the old visit's frames on most loads.

## Stage B: record and replay

- **Recording:** every game writes `user/log/coop-trace-<time>.txt`. Each line is a timestamp
  (UTC ms) plus one record: P2P frames in and out (type, peer, payload hex), board requests and
  responses (endpoint, body), place transitions, lifecycle events, and the per-tick inputs of
  the pure logic (`SharedDamage`, `VisitKills`, the HP board), sampled only on change. Size is
  capped, so the file rotates like the save backups.
- **Replay:** a test tool feeds a trace back into the pure classes and prints their decisions
  (kills posted or withdrawn, HP shown, hides). Unit tests check in short traces cut from real
  sessions as regression cases. Game memory itself is not replayed. This covers the sync
  decisions, which are where most desyncs start.

Built (2026-10-12). Every co-op game writes `user/log/coop-trace-<yyyymmdd-hhmmss>-<process>.txt`.
A file holds at most 50 MB, then a new one starts, and the newest 5 are kept. A writer thread
writes the lines. A full queue drops lines and counts them (a `trace-dropped` record). It is on
by default; `SHADPS4_BLOODBORNE_TRACE=0` turns it off. The record kinds are listed at
`CoopTraceVersion` in `bloodborne_party.h`. A board body is cut at 8 KB, and a body that
repeats the endpoint's last one is written as `same=1`. The `-in` records hold the inputs of
`VisitKills`, `SharedDamage`/`LowerToHostHp`, `ShownLikeTheHost` and `AppliesMemberKill`; a
key that is left out is unchanged. Each `OnLifecycle` event is a `life` record with its session
epoch, and each change of place a `place` record.

- **Read a player's trace:** `check_party_log.py --trace <their shad_log.txt>` summarizes the
  traces next to the log: frames per type, board failures, and peers silent for more than 5 s
  (outage candidates).
- **Replay one:** set `BLOODBORNE_REPLAY_TRACE=<trace>` and run
  `shadps4_bloodborne_test --gtest_filter=CoopTraceReplay.ReplaysTheTraceNamedInTheEnvironment`.
  It prints each decision after its record's time.
- **Cut one into a test:** copy the lines around the bug into
  `emulator/tests/bloodborne/traces/<name>.txt`. The first record of each `-in` kind must carry
  all of its keys, with whole lists (`chars==...`, `alive==...`). Each new file starts that way;
  for a cut from the middle, fill in the first record from the earlier ones. Keep the `life`
  and `p2p-in` lines in between, and add `#` lines that say what it shows. Replay it as above, check the
  decisions, and save them as `<name>.expected`. `CoopTraceReplay.CheckedInTracesKeepTheirDecisions`
  then keeps them.

## Stage C: world log over the direct link

- **Log entry:** `{seq, epoch, map, kind, payload}`. Kinds: `Kill`, `Unkill` (reset),
  `FlagSet`/`FlagClear` (by the flag table's owner rules), `Pickup`, `Shown` (enable/park/dead
  bits), `HpFloor` (enemy HP agreed), `Reset(map)` (wipe or respawn).
- **Ordering:** the host orders the log. Guests send proposals (their kills, damage and flag
  changes) to the host over the P2P link, and the host appends them and sends the entries to
  everyone. Members apply entries in `seq` order, exactly once.
- **Joining and gaps:** on join or after a gap, a member asks for a snapshot (current state per
  map plus the last `seq`). The server boards stay as a fallback copy of the snapshot, for when
  the direct link is down. Tonight's 15–31 s outages hit the board polling.
- **Migration:** one board at a time, each its own PR with the old path kept behind a switch
  until rig-checked. Order: kills, shown, flags/mapstate, HP.

Built, step 1: the kills (2026-10-12, PR #46, rig-checked and merged). The pure logic is in
`bloodborne_party.{h,cpp}` (`WorldLogHost`, `WorldLogMember`, `WorldLogPath`, the frames) and the
runtime in `bloodborne_world_log.cpp`.

- **Proposals are whole sets.** Each member sends its standing kills (`VisitKills::Standing`: this
  visit's and the held ones, by map) to the host at once on a change and every 0.5 s (frame 0x34,
  `WorldLogProposal`, with its run and a number that only grows, so a late frame and a restarted
  game's old kills are told apart). The host keeps every member's set and appends an entry when
  the party's union changes: a `Kill` for an enemy's first killer, an `Unkill` when its last one
  takes it back. Two members' kills of one enemy are one entry; who proposes a kill is still
  decided as before (the killer only, PR #40). A member that left keeps its kills standing, as on
  the board.
- **Entries** go to every peer in batches of at most 100 (frame 0x35, also the heartbeat every
  0.5 s with the host's last seq). Members apply them in seq order exactly once; a gap, a late join
  or a new epoch past its start asks for a snapshot (0x36), which the host answers in parts of at
  most 280 kills (0x37). A member's copy stands 3 s after the host's last frame; otherwise it reads
  the kill board.
- **Epochs:** `(host run << 32) | logs started`. The host starts a new log (a `Reset` of every map
  and no kills at all) on its wipe, a join, a leave or a new character. A member forgets its copy on
  its own join, leave or new character and follows a newer epoch (or another host run); an older
  log's frames, and proposals made for one, are dropped.
- **Wipes** (fixed after rig smoke 2026-10-12, run 191219: the dog stood dead in all three games
  after the party wipe): the first version kept the members' sets in the wiped log, so the host's
  own pre-wipe kill stood again at once, and a guest's proposal stood it a third time after the
  wipe's trip. That guest had applied the host's kill near (the game's kill, `died_for_others`), and
  the kill counted as its own in its visit; it read the kill board's pre-wipe copy while its log
  was not fresh. Now the wiped log starts with no kills and its `Reset` says so
  (`WorldLogResetAfterWipe`). A member that hears it proposes nothing until its own wipe has
  forgotten its kills, or 15 s went by (`WorldLogWipeHold`; a member elsewhere has no wipe). For
  15 s after either wipe it leaves out the host's entries of the kill board. A member never
  proposes the kills it applied for another member (`died_for_others`).
- **The path:** every member runs one build (Kaniel 2026-10-12; no mixed builds). The log runs
  while this game has it on (`SHADPS4_BLOODBORNE_WORLD_LOG=0` turns it off) and is linked to the
  room's peers; a moment without peers (a dropped link, a load) keeps it 5 s, then the boards. The
  path in use is logged once per change (`Co-op world log: on, ...` / `off, ... (<why>)`). A peer on
  another build, or with the log switched off, is an error the hello check logs (`Co-op: <peer>
  runs build ...; world log off, this game's on`), not a path of its own. On the boards everything
  is as before (the kill board and the direct kills frame 0x32).
- **The board underneath:** on a change of path a member's next kill post asks since FreshStart,
  so shadNet takes back what it posted. On the log path members post no kills; the host posts the
  party's union as its own, the copy for a member whose link is down or who joins. A member reads
  the log plus the board's other entries (members out of the room), or the whole board when its
  copy is not fresh. The server is unchanged.
- **Trace:** `wlog-in` (the host's starts and the proposals that changed its log, a member's
  batches and snapshots that changed its copy, its resets) and `wlog-path`. The replay runs them
  through the same classes; `traces/world-log-kills.txt` is a checked-in case.

Built, step 2: the shown characters (2026-10-12, not rig-checked yet). The host's map and each of
its characters' `CharacterShown` bits (hidden 1, switched off 2, far from the host 4, dead in its
world 8, as on the shown board) go on the same log.

- **Entries:** `ShownMap` (the host's characters are of this map from now on; its load starts
  them over), `Shown` (a character's bits, sent as kind byte `0x10 | bits`, so the entries frame
  keeps its 9-byte layout) and `Unshown` (the character left the host's world). The host logs only
  what changed since its last `Show`, from the same list it posts to the shown board each kill step;
  a new log repeats them after the kills.
- **Snapshots:** the host answers a snapshot request with the kills' parts (0x37), then the shown
  characters' parts (0x38, at most 240 a part) and the map state (0x3A, step 3). A member takes a
  snapshot only once all are in, so its copy of the characters never misses entries.
- **What a guest follows:** the log's copy when it is fresh and whole and the host's map is known,
  turned into the same list the board gave (`HostShownNow`); else the shown board's, which the host
  keeps posting. Everything after that is unchanged: `HostNearStates` (a window NPC keeps what the
  host's world had while the host was near), the far-away parking bit, the switches
  (`SwitchedLikeTheHost`: Iosefka's copy by her door) and the host's dead bit for guests apart.
- **Story removals over a load** (`StoryRemovals`, 2026-10-16): a map's story removes some
  characters (2004:29, the hide byte) seconds after every load, 3.5 to 13 s on the rig, so each game
  drew them until then (Gilbert by the second Central Yharnam lamp after a trip, Iosefka). Every
  game remembers per map what its story removed (a guest also what it hid to match the host) and on
  its next load switches those off as they appear, until the story removes them again or 20 s pass.
  The host's first load and a trip's arrival now count as loads (`StartsNewWorld`), so the host no
  longer posts its characters before its story ran. Rig: story-npc-trips (`watch-unseen`).
- **The path:** the kills' (one build: no hello bit of its own; co-op protocol 3).
- **Trace:** `wlog-in` ops `show` (the host's list when it changed the log) and `shown-snapshot`.
  `traces/world-log-shown.txt` is a checked-in case.

Built, step 3: the shared flags and the host's map state (2026-10-12, not rig-checked yet). The
log carries exactly what the flag boards carried, decided by the same code. The shared flags are
`FlagsToShare` of each change that passed the bulk guard (`IsBulkFlagChange`): the flag table's
`Shared` rows, a lamp's active flag and a boss fog's entry. Never the bell's session flags, a boss's
"found" flag, the Dream doors, the badge lines, a headstone or lamp trip's request, or a map
pickup. The map flags are the host's map bitmap, of which a guest's load takes the
`TakesHostMapFlag` ones.

- **Flag events** go from a member to the host in frame 0x39 (`WorldLogFlagProposal`). Each event is
  numbered within its run, and the frame is resent for 10 s, so a lost frame costs nothing. The
  host appends a `FlagSet` / `FlagClear` (one entry for one flag turned the same way within 10 s:
  two members at one lamp), sets it in its own world and posts it to the flag board. Every member
  sets it through the old queue (`flags_to_set`, applied while sharing). These are events, not
  state: a snapshot does not carry them, as a game's first flag board post gets none of the earlier
  ones. A member that lost an entry gets the flag board's copy, and an event counts once from either
  copy within 10 s (`FlagEventOnce`).
- **Map state:** `MapStateMap`, `MapFlagOn`, `MapFlagOff` entries from the host's 3 s snapshot
  of its map (`SnapshotHostMapState`), only what changed; a new log repeats it. A snapshot answer
  ends with the map state (frame 0x3A), and a member takes a snapshot only once it is in. A guest's
  load and the host's map (`HostMapFromItsPost`) read `HostMapStateNow`: the log's copy while it is
  fresh and whole, else the map state board's, which the host keeps posting.
- **Path:** the flags take the log with the kills (every peer runs one build, Kaniel 2026-10-12).
  `SHADPS4_BLOODBORNE_WORLD_LOG=0` keeps them on the boards. Leaving the log, the events not taken
  yet go to the flag board.
- **Trace:** `wlog-in` ops `propose-flags`, `map-state`, `map-state-snapshot`;
  `traces/world-log-flags.txt` is a checked-in case.

Built, step 4: enemy HP (2026-10-12, not rig-checked yet). What the HP board and the direct damage
frames (0x30) carried: each member's report (`EnemyHpReport`: the damage its own hits did per enemy
this visit, or in one world the host's HP, `HostHpReport`), by source (the member's tag and visit).

- **Proposals:** a member sends its whole report to the host (frame 0x3B, `WorldLogHpProposal`) at
  once when it changed and every 0.5 s, as it sent the direct frame. The host's own goes straight
  in. The host logs only what changed: `HpSource` (the source and map the next entries are of),
  `HpValue` (an enemy's damage or HP, and its max), `HpGone`, `HpSourceGone` (a new visit or a
  restart made a new source). A report with no enemies is none.
- **Every hit once:** the reports are cumulative per source, so `SharedDamage` takes each source's
  damage once however often the same report comes (test `EachHitCountsOnceThroughTheLog`). A
  guest's hit reaches another guest in two hops (to the host, then out) instead of one. A frame
  from the host is read at once and the enemy step runs on the next frame, so the lag stays near
  the direct frames' (rig hit3 to be checked).
- **Wipes:** a new log has no HP. A member that hears the wiped log holds its reports until its own
  wipe (`WorldLogWipeHold`, as for the kills), and the reports from before the reset go into
  `SharedDamage::ForgetDamageBefore` as before.
- **Snapshots:** the HP reports follow the map state (frame 0x3C, at most 100 enemies a part), and a
  member takes a snapshot only once they are in. Every entry now carries a 32-bit value (13 bytes;
  co-op protocol 5).
- **Backup:** (step 4 as built) every member still posted its report to the HP board; the world
  snapshot below replaced that. On the boards (the log off), everything is as before.
- **Trace:** `wlog-in` ops `propose-hp` and `hp-snapshot`; `traces/world-log-hp.txt` is a checked-in
  case. The `hp-in` records are unchanged: their `reports` are what the log gave.

Built: the world snapshot on the server (2026-10-12, not rig-checked yet; Kaniel approved). While
the world log runs, the kill, shown, flag, map state and HP boards are not polled. The live sync
goes only over the link, and the server keeps one copy of the host's world.

- **The host** posts its log's state every 2 s to `POST /bloodborne/party/world` as one blob: the
  same snapshot frames it sends a member that asks over the link (kills, shown characters, map
  state, HP reports, recent flag events), each with its type and length.
- **A member** reads it only while its own copy is not fresh: a join, a restart of its game or the
  host's, or its link to the host down. It takes the frames as it takes them from the host. A copy
  older than 30 s, or of another log of the same host run (an older one), is not taken.
- **Flag events in snapshots:** a snapshot now ends with the log's last 64 flag events (frame 0x3D).
  A member that missed some in a gap sets them; one that joins gets none of the earlier ones, as a
  game's first flag board post got none. This replaces the flag board's copy.
- **HP fast path:** every member still sends its report straight to the peers (0x30) as well as to
  the host's log. The bars move on the first hop, and `MergeEnemyHpReports` takes the most damage
  of the copies of one report (per member and visit), so nothing counts twice. A member waiting for
  its own wipe sends neither.
- **The boards** stay for games with the log off (alone, or `SHADPS4_BLOODBORNE_WORLD_LOG=0`); the
  server keeps their code. The map, rest, echo, trophy and vitals boards and matchmaking are
  unchanged.
- Server: `PartyWorldSnapshotBoard` (the party's latest blob and its age), unit tests, and the
  `world.*` messages in the contracts file. Co-op protocol 6.

## Stage D: split `bloodborne_re.cpp`

The split is a move of code only, with no behaviour changes, checked against the clang-tidy
baseline (2026-10-12: 330 functions, 44 over cognitive complexity 25, holding 41% of the
lines). The main seams:

- patch table and installer
- flag store and progression
- kills and HP
- travel and place
- party death
- boards and P2P
- rig autoplay
- diagnostics and tracers

`RunAutoplayStep` (703 lines, complexity 464) becomes a table of verbs.

Done (2026-10-12): `bloodborne_re.cpp` keeps the core (memory reads, place, lifecycle, the
party tick) and the rest is in `bloodborne_patches`, `_join`, `_travel`, `_flags_store`,
`_kills_hp`, `_party_rules`, `_party_death`, `_boards`, `_autoplay`, `_trace_diag` and
`_re_tracer.cpp`. What one file uses from another is declared in `bloodborne_re_internal.h`
(namespace `Coop`); the RE trace sites and shared eboot offsets are in `bloodborne_re_sites.h`;
everything else keeps its file's anonymous namespace. `tools/re/code_health.py` measures every
function (cognitive complexity, lines, nesting) and `--check` (ci-check.ps1 `-CompileDb`) keeps
new code under cognitive 25, nesting 4 and 120 lines, and old code from getting worse than
`tools/re/code_health_baseline.json`.
