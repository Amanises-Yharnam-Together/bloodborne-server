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
  queued and applied when it ends.
- **Visit stamps:** every P2P frame and board post carries the sender's `(session epoch,
  visit)`. The session epoch is bumped on a party wipe, a leave, a new session or a new
  character. A receiver drops anything stamped with an older epoch, and for map-scoped data
  anything from an older visit of that member on that map. Today's reset flags
  (`party_kills_reset`, `party_hp_reset`, …) become one dispatcher, `OnLifecycle(Wipe | Trip |
  Leave | NewCharacter)`, where every per-visit container registers its reset.

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
key that is left out is unchanged. Place transitions come with Stage A.

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
