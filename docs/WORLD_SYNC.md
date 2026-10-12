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

Built, step 1: the kills (2026-10-12, not rig-checked yet). The pure logic is in
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
- **Epochs:** `(host run << 32) | logs started`. The host starts a new log (a `Reset` of every map,
  then the standing kills) on its wipe, keeping the members' sets, and on a join, leave or new
  character without them. A member forgets its copy on its own join, leave or new character and
  follows a newer epoch (or another host run); an older log's frames are dropped.
- **The path:** the hello carries a features byte (co-op protocol 2), and the log runs only while
  every room peer has it on (`SHADPS4_BLOODBORNE_WORLD_LOG=0` turns it off). A peer whose hello
  has not come, or no peers at all, keep the path 5 s, then the board. Builds before protocol 2
  drop the new frame types. The path in use is logged once per change (`Co-op world log: on, ...`
  / `off, ... (<why>)`). On the board path everything is as before (the board and the direct kills
  frame 0x32).
- **The board underneath:** on a change of path a member's next kill post asks since FreshStart,
  so shadNet takes back what it posted. On the log path members post no kills; the host posts the
  party's union as its own, the copy for a member whose link is down or who joins. A member reads
  the log plus the board's other entries (members out of the room), or the whole board when its
  copy is not fresh. The server is unchanged.
- **Trace:** `wlog-in` (the host's starts and the proposals that changed its log, a member's
  batches and snapshots that changed its copy, its resets) and `wlog-path`. The replay runs them
  through the same classes; `traces/world-log-kills.txt` is a checked-in case.

Next: `Shown` (the shown board: enable, park and dead bits per character), then `FlagSet` /
`FlagClear` and the map state (by `FlagTable`'s owners), then `HpFloor` (enemy HP agreed), each
moving one board onto the log the same way, with the board as the fallback copy.

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
