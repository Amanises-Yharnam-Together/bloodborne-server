# Seamless travel: plan and scope

Kaniel's goal (2026-10-06): the party stays connected no matter where each member is. Anyone can
travel anywhere by themselves (lamps, the Dream, headstones), nobody gets pulled along or sent
home, and members on the same map see and fight with each other. This is essential to the
seamless experience, and we are willing to rework the connection model to get it.

## How 01.09 connects players today
- **Transport (ours, emulator + shadnet):** NP matching2 rooms and signaling run through shadnet.
  Game packets travel as P2P UDP between the emulators. Nothing in this layer cares about maps,
  so it can stay as it is.
- **Session (FromSoftware's, native):** `CSMultiPlayMan` (+0x124 state: 3 host, 6 summoned guest)
  and the session object at GlobalState +0x16F8. One host world, and guests are phantoms loaded
  into it with a summon reload. Everything in the world is the host's: enemies, AI, flags and
  items. Guests receive it.
- **Where the game ends a session:**
  1. On any map change. Once the new stage has loaded, a changed stage uid calls `LeaveSession`
     (`0x0193C689`, inside the world load).
  2. On world teardown. Our hook (`0x0194716E`) calls `StopRequest` for hosts and guests, which
     was needed because the keep-matching patch keeps rooms alive.
  3. Event scripts: boss kill (BlockClear2_2/2_3, patched on `phase3-travel`), deaths
     (patched in Phase 2), `OnLeave_Limit` 5 s after any leave (patched for party guests),
     kicks and errors.

## The options

### A. Stretch the native session across maps
Keep one native session and remove the places that end it. Members on other maps stay session
members. The game hopefully just doesn't draw them until they share a map again.
- **Already built (untested):** the trip marker at the stage transition `0x013CDE30`, which skips
  leave 1 and 2 during a trip.
- **Open questions, answered by experiments:**
  - E1: after the host's trip, does the session stay up on both sides (state 3/6, no disconnect
    message), and does the guest's world keep working? The host's map is gone from the host, so
    who runs the guest's enemies?
  - E2: when the host comes back to the guest's map, does the guest's phantom reappear and sync?
    Or does the game need a fresh summon to create the remote player again?
  - E3: a guest trip. A phantom loads a new map while still in the session. Whose world does it
    load: the host's flags, its own save, or neither?
  - E4: three players on three maps, for packet volume and whether remote-player slots collide.
- **Likely work if it holds up:** hide and park remote players that are on another map; put
  them back on a shared map; give guests lamps (their missing lamp prompt); decide whose world a
  map is when the host isn't on it.
- **Scope:** small if E1 and E2 pass, unknown after that. This is the cheapest path and is
  already half built.

### B. A party layer with automatic re-summons (hybrid)
Our party layer (shadnet presence plus the emulator) always knows every member and their map. The
native session only exists while members share a map. Every trip quietly ends it, and
meeting on a map rebuilds it with no bells and no menus.
- The joining load is folded into a load the player makes anyway. A guest travelling to the
  host's map gets its trip turned into the summon reload, which the cross-map join's stage
  handoff already does. A host travelling to a guest's map makes the guest re-summon (one load for
  the guest, unless roles swap; see below).
- **Needed:**
  - presence: shadnet tracks each member's map, area and block, which the rest board already
    half does;
  - a guest stays in its own world at its own position when apart (instead of the last lamp);
  - instant joining: long-poll matching, the HANDOFF "near-zero join wait";
  - joins placed at the player's position, not the summon spot;
  - optional role swapping: whoever is already on the map hosts.
- **Limits:** while apart, each member is in their own world, which is fine. Meeting again costs
  the guest one load, so it is seamless in feel but not literally zero loads.
- **Scope:** medium. Mostly matching, presence and placement work in code we already own and
  understand.

### C. Our own netcode (every player in their own world)
Replace FromSoftware's session: everyone always plays their own world and we replicate players
ourselves (transform, animation, equipment, HP, hits), the way the Elden Ring and Dark Souls 3
Seamless Co-op mods extend their games' sessions.
- **Hard parts:** shared enemies. Each world simulates its own enemies, so the same boss exists
  twice. We would need to pick an owner per enemy and sync AI, HP, deaths and loot, which is the
  largest part of any game's netcode, done here blind through reverse engineering. Then player
  damage, parries, visceral attacks, items and summons.
- **Scope:** large: months of reverse engineering, high risk, and every game subsystem touches
  it. Only presence (seeing each other's avatars) is a reasonable first slice.

## Recommendation
1. **Run A's experiments now.** The build is ready, and one rig session answers E1 and E2. If the
   game tolerates a session spread over maps, A is by far the shortest path to what you want.
2. **If A fails at E1 or E2, build B.** It reuses everything Phases 1-3 built, and every part
   of it is plain engineering in code we control.
3. **Keep C for later, and only as presence.** A full netcode rewrite isn't worth starting
   unless A and B both hit a wall we can't get past.

## Milestones (A first, B as fallback)
| # | Milestone | Done when |
|---|---|---|
| 0 | Boss kill keeps the party | guest stays after Cleric Beast (built) |
| 1 | E1/E2: host trip keeps the session | host goes to the Dream and back, guest stays and reappears |
| 2 | Guest lamps | guests rest and travel from lamps like the host |
| 3 | E3: guest trip | a guest travels alone and the host stays put |
| 4 | Meeting again | members on the same map see each other with no action needed |
| 5 | Whose world | rules for flags, items and enemies on a map the host isn't on |
| 6 | Three players | E4 and the existing "more than 2 players" item |

Every milestone follows the usual rules: red-green tests for the pure logic, all CI checks,
and a rig test by Kaniel before merging to main.

## Risks
- Native session code may assume one stage per session in places we can't see (crashes,
  desyncs). Mitigation: experiments before building on it, plus diagnostics in the logs.
- The rig is two instances on one PC near the RAM limit, and long sessions with trips stress it.
- Quit-to-title must still end the session. The trip marker only covers the stage transition,
  so this needs checking in E1.
