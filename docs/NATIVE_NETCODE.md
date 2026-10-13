# Bloodborne's own netcode, and a plan to run co-op on ours only

Research for Kaniel, 2026-10-13: how the game's native multiplayer works (summons, session, players,
HP and damage, enemies, world state), compared with our co-op layer, and a phased plan for a mod that
takes the game's networking over. Static RE of the 1.09 eboot (image base 0, the same addresses our
co-op code uses). Nothing here is rig-verified yet; guesses are marked. The per-channel detail is in
`docs/native-netcode/` (characters.md, world.md, session.md).

Tooling: morluto/rea was installed (WSL, Ghidra 12.1.4) but auto mode refused to run it, so the work
used our capstone scripts plus a new flattener that applies the eboot's relocations and names its
imports (vtables and import calls become visible; the old `eboot.img` has no relocations).

## The short version

- **Every game packet goes through one send and one receive function.** `0x017894E0` sends
  `(NetSync, member, channel, buffer, size)`; `0x01789200` pops the next packet of a channel from one
  member's queue; `0x017893E0` / `0x01789A40` are the "from host" / "to host" variants. A packet is
  `{u32 channel, u32 size, payload}` (max 0x6400). There are 68 channels (0x00-0x43, about 60 used).
  Channels 0-6 are unreliable, 7 and up reliable. One choke point means a mod can see, drop, rewrite
  or inject any game packet from two hooks.
- **The transport under it is already ours at the bottom, but not in the middle.** Game packets go
  down through NexusRevolution2 "ExtNetwork2" (its own record framing, reliability streams and
  per-member demux) to `sceNetSendto` on a P2P socket, which our `p2p_sockets.cpp` carries over one
  UDP socket. Rooms, signaling and the sign server are ours (shadNet, our NP emulation). The framing,
  reliability, per-member queues and the join handshake are all still game code.
- **The native model is one host's world.** The host runs every enemy (ownership word at
  WorldChrMan+0xA40), sends enemy positions (ch 4), HP (ch 5), catch-up records (0x29) and join-time
  animation (0x36); a guest's hit on an enemy is sent to the host (ch 0x15), the host applies it and
  kills; the host's own hits send nothing. A joining guest gets the host's flags for the host's
  current map only (ch 0x10), the host's dead enemies, objects and decals (ch 0x11) and live bullets
  (0x3F), once. After that there is no flag replication: only `CompleteEvent` sets (0x0F, on only),
  remote Lua event calls (0x0E) and EMEVD instance sync (0x31) keep worlds in step.
- **Players sync themselves, peer to peer.** Each game sends its own player on ch 6: position,
  heading, HP and the Havok behavior graph's events and variables (that is how animation travels,
  not by animation ids). Hits on a player go to the victim's game (ch 0x14, the whole 0xEC-byte hit)
  and the victim decides its HP.
- **Seamless co-op breaks the native model's assumptions,** and that is where our bugs came from:
  members on different maps still exchange host-scoped packets (0x15 guest hits reaching the host's
  copy = the double damage; the host's hits never sending = the late bars), the join snapshot covers
  one map, and the session machinery (timeouts, kicks, the boss "thanks" kick 0x27) assumes a visit.
  We patch around it with ~120 sites, SHAD frames 0x30-0x3E and the HTTP boards.

## Native netcode map

### Session objects
| Pointer | Object | Key fields |
|---|---|---|
| [0x0553D6D0] | NetSync (vtable 0x0532C090: event handler 0x0178B5E0, receive 0x0178D510) | state +0x84 (0 none, 1 guest joined, 2 host world data applied, 3 loading, 4 in world), peers/queues +0x28, pending members +0x48, member to slot +0x68 |
| [0x05540290] | SprjSessionManager | host member +0xD0, members +0xF8..+0x100 (0x20 each), matching state +0x124 (3 hosting, 6 summoned guest), resend queue +0x188/+0x1B0, update 0x01ED0B20 |
| [0x056C6D70] | member identity registry | a member id is {identity*, lock}; the identity holds a SceNpId (+0x36): peers are addressed by NpId |

Correction to a repo comment: SprjSessionManager+0xD0 is the host member, not the local one
(`bloodborne_re_internal.h:499`).

In NetSync states other than 4 only channels 7, 8, 0x0A-0x0D, 0x10, 0x11, 0x19 and 0x42 can be sent,
and unreliable player/enemy channels are dropped on receive: the join handshake runs first.

### Summon and join (host summons guest)
1. Guest posts its sign (`/summon_messenger/create`); the host's bell searches (`/get`, every 5 s).
2. Host: QueueInsert 0x01875320 -> 0x01876060 -> NetSync CreateSession 0x01788ED0 -> SSM 0x01ECFCD0
   -> `sceNpMatching2CreateJoinRoom`. Session event 7: NetSync state 4. Host claims the sign (`/request`).
3. Guest gets the claim in its next `/create` reply; the summon manager update 0x01872360 parses it
   (u32 -> GameMan+0x16F0, slot byte -> +0x16F4, join data 0xC90DD0) and calls JoinSession 0x01789010
   -> `sceNpMatching2JoinRoom`; per peer: `sceNpLookupNpId` -> `sceNpSignalingActivateConnection`.
   Event 8/9: state 1, Lua OnJoinMutiplay.
4. Host, event 0xE "member joined": join task, remote GameDataMan slot (4 max), peer queues; sends
   ch 7, 0x19, 0x10 (flags, deflate "MNSE"), 0x11 (decals "LCED", objects "OBJ ", characters "CHR "),
   0x3F (bullets), assigns enemy ownership (0x018F0DB0). Everyone sends ch 0x42 (own player slot).
5. Guest applies 0x11 then 0x10 (state 2), runs the summon reload, world load sets state 4 and asks
   for character data (ch 0x0A); both sides exchange ch 8/0x0B/0x0C/0x0D/0x13/0x3B/0x3C; join tasks
   finish and the remote PlayerIns appear.
6. Leave: event 0xF; timeout/overflow/resend backlog > 1000 bytes raise Lua OnIrregularLeaveSession
   and a stop request; event 0x1E is OnBeKickOut; the host's 0x27 after a boss is the "thanks" kick.

### Channels by area
| Area | Channels | Who sends | Notes |
|---|---|---|---|
| Join handshake | 7, 0x19, 0x42, 0x10, 0x11, 0x3F | host to joiner (0x42 all) | 0x10 = flags of the host's current map + global groups, once |
| Character data | 8, 0x0A, 0x0B, 0x0C, 0x0D, 0x13, 0x3B, 0x3C, 0x43 | each player | stats/face/name, gear, after each load; 0x0A = "send me yours" |
| Player state | 6 | each player to all, unreliable | position, heading, HP, behavior graph events/variables |
| Hits on players | 0x14 | attacker to victim's game | whole hit replayed by the victim |
| Forced animations | 0x24, 0x25 | any | 0x25 asks a player's game to play an anim (grabs) |
| Throws (visceral) | 0x12, 0x37, 0x30 | any | throw start/pair/events |
| Enemies | 4 (pos, owner), 5 (HP, host), 0x15 (guest hit to host), 0x16/0x17 (near lists), 0x29 (catch-up), 0x36 (join anims), 0x3D/0x41 (state records, any), 0x34 (unknown) | host-centred | NetWorldChrSync at WorldChrMan+0xA40, body 0x018ECEB0 |
| Events/flags | 0x0E (Lua calls), 0x0F (CompleteEvent flag on), 0x31 (EMEVD instance target/value) | any to all | 0x31 is how a remote player's trigger (fog, lever event) runs everywhere |
| Objects | 0x2D (act + actor), 0x2E (enable), 0x2F (host's state, guest forced after 10 s), 0x2A (breaks), 0x2B (object anims) | any / host | |
| Projectiles | 0x3E (new bullets), 0x40 (bullet updates, guess), 0x3F (join snapshot) | any / host | |
| Items | 0x2C (map items), 0x39 (counter 0..99 transfer, guess Insight) | | 0x2C already neutralised by us |
| Session/misc | 0x27 (boss thanks kick), 0x32/0x33 (join/leave announcements), 0x22 (dialog vote), 0x38 (shared timer, unknown) | mostly host | |

Unknown still: the 0x60-byte enemy state record (0x29/0x3D), the 0x14 record (0x41), ch 0x34's
payload, ch 0x43's meaning, the ch 7 receive site, where OnP2PTimeOut is raised.

## How it compares with our layer

| Concern | Native | Ours today |
|---|---|---|
| Finding each other | bell + signs on /summon_messenger, matching2 room | same path, our server; party auto-rings; quick re-summon |
| Transport | ExtNetwork2 records/reliability over P2P vports | P2P vports on one UDP socket (ours); SHAD frames beside the game's on vport 0xFFFF |
| Session lifetime | one visit; leaves/kicks/timeouts end it | ~120 patch sites keep it alive across maps, trips, deaths, boss kills |
| Players | ch 6 peer to peer, ch 0x14 hits | native; we re-request character data (0x0A) and re-create members |
| Enemy HP | host authoritative (ch 5), guest hits to host (0x15) | per-member copies + own-hit counting, SHAD 0x30 direct damage, world log HP (0x3B/0x3C), HP board |
| Kills | host's world, join snapshot 0x11 | kill board, world log 0x34-0x37 |
| Flags | join snapshot of one map, then event-driven | flag table, world log flags 0x39/0x3D, flag/mapstate boards |
| Items | 0x2C host-driven | packets read as empty, each keeps its own |

So two systems run side by side: the game's host-centred sync on every map, and ours on top, with
hooks deciding case by case which one wins. The mod below makes that decision once, per channel, at
the choke point.

> **Superseded (2026-10-13):** Kaniel chose a server design: the server owns the world and carries
> all traffic, with a team lobby in the game's own menus. The current plan is
> docs/SERVER_COOP_PLAN.md; the sections below are the first, P2P version, kept for reference.

## The mod: what can be replaced

**Replace:**
- **The middle transport** (ExtNetwork2 framing, reliability, per-member queues): hook the send
  (0x017894E0, plus 0x01789A40) and feed received packets into the peer queues ourselves (the same
  job as 0x0178D510 -> 0x01790C20). Game packets then travel as SHAD frames on our one link with our
  ordering, resend, epochs, ping and outage detection. This is the piece most likely to remove the
  intermittent join stall (signaling up, the game gives up 5.4 s later) and the irregular leaves.
- **Session membership and summons**: membership comes from our party, not from signs and the bell.
  First step, below the game: our NP emulation answers CreateJoinRoom/JoinRoom and signaling from the
  party roster at once (no sign search or claim delivery); the game's own room code still runs.
  Second step, if needed: drive NetSync's session events (7, 8/9, 0xE, 0xF) directly.
- **Host-centred world state**: ch 0x10/0x11/0x3F join snapshots, 0x0F flags, 0x2F object
  corrections, 0x2C items, 0x27 kick: drop or rewrite them and serve the same state from our world
  log, which already knows every map, not just the host's current one.
- **Enemy authority**: decide ownership per map in our code (one owner per map: whoever runs it, as
  the lone-guest ownership word already does) and gate 4/5/0x15/0x29/0x36/0x16/0x17 by it: pass them
  between members on the same map with that owner, drop them otherwise. Damage then has one path, not
  three.

**Keep native (gate by place only):** ch 6 player state (rewriting Havok behavior replication is a
project of its own), 0x14 player hits, 0x24/0x25 animations, throws, bullets 0x3E/0x40, objects
0x2A/0x2B/0x2D/0x2E, character data, event sync 0x0E/0x31. These are correct when both players stand
on the same map; the mod only drops them between members on different maps (the PlaceTracker
decides).

## Hooks needed

| Hook | Address | Purpose |
|---|---|---|
| Packet send | 0x017894E0 (entry), 0x01789A40 (to host) | per-channel policy; tunnel into SHAD |
| Packet receive | 0x01789200 / 0x017893E0 (pop), or the fill path 0x0178D510 -> 0x01790C20 | policy on receive; inject tunnelled packets into peer queues |
| Resend / timeouts | 0x01ED0B20 (resend queue, idle timer +0x2E0/+0x2F0) | our link decides liveness, not the game's timer |
| Session events | 0x0178B5E0 (vtable slot 0 of 0x0532C090) | observe; later synthesize joins/leaves |
| Summon path | 0x01872360, 0x01875320, 0x01788ED0, 0x01789010 | later: party join without signs |
| Join snapshots | 0x01786050 (0x10), 0x01786360 (0x11), 0x01A04190 (0x3F), apply 0x013BECA0 | replace with the world log's snapshot |
| Enemy sync | 0x018ECEB0 sends of ch 4/5/0x29/0x36, 0x01A29E40 (0x15) | ownership gate |
| NP emulation (ours) | `np_matching2*.cpp`, `np_signaling.cpp`, `p2p_sockets.cpp` | party-driven rooms and links |

Each goes in `bloodborne_patch_sites.h` with its byte check, as today.

## Risks

- **NetSync state gates everything.** Remote PlayerIns, join tasks and char data only progress
  through states 1 -> 2 -> 3 -> 4 and the join handshake; skipping a native packet (0x10, 0x11, 0x42)
  without supplying its effect stalls the join. Each replacement must reproduce the state change.
- **Reliability semantics.** Game code assumes reliable channels arrive in order per member, and
  unreliable ones may drop (queues drop the oldest). Our tunnel must keep per-member, per-class order.
- **Identity is the NpId.** Member ids compare NpIds; a synthesized member must carry a real
  identity object the registry knows.
- **Unknown payloads** (0x60/0x14 enemy records, 0x34, 0x43): pass them through untouched until known.
- **Host authority is baked in** (`+0x124 == 3` checks on send, `== 6` on receive for 0x10/0x11/0x2F/
  0x27/0x32/0x33/0x3F, ch 5 host-only). Moving ownership off the host means patching those checks or
  forging the state for one call, as our ownership-word work already does.
- **Overlap with existing fixes.** Many of today's hooks (map item header, keep local flags, leave
  limit, kill and HP boards) do part of this; the plan retires them one at a time behind switches,
  rig-checked, so nothing regresses silently.
- **One build for all** (protocol bump on the SHAD hello); no mixed builds.
- **Static RE only.** Every finding should be confirmed with the phase 0 trace before code relies on it.

## Phased order

0. **Trace (no behavior change).** Hook the send/receive choke points and write every game packet
   (channel, size, peer, sender's place, NetSync state) to the coop trace. Rig: join, trip, hit3,
   boss-kill. Confirms the channel map, settles the unknowns, measures traffic per channel.
1. **Channel policy table.** One table `{channel -> pass | drop | drop-if-apart | ours}` at the choke
   point, read by send and receive. Start with what we already do elsewhere (0x2C, 0x27) and
   map-gating for event/object/bullet/enemy channels between members on different maps. Retire the
   hooks it replaces.
2. **Enemy ownership.** One owner per map from our code; gate ch 4/5/0x15/0x29/0x36/0x16/0x17 by it;
   retire own-hit counting and the HP board once the world log HP matches (rig hit3, hp-apart,
   guest-hit-once).
3. **Tunnel the game's packets.** Send game packets as SHAD frames on our link, feed the peer queues
   ourselves, and take liveness from our link (resend queue and idle timer stay quiet). Rig: join x25
   (the stall), host-crash, crash-rejoin, wipe3.
4. **Join from the party.** Rooms and signaling answered from the party roster (no signs or bell for
   party members), then the join snapshots (0x10/0x11/0x3F) served from the world log for any map.
   Rig: join, join-apart, join3, early-trip, resume.
5. **Clean up.** Remove superseded boards and patch sites; the server keeps only the party roster,
   accounts and the world snapshot fallback.

Phases 0 and 1 are low risk and give the evidence for the rest; 3 and 4 are the big ones.

## Reproducing the RE

Scripts (scratch, to be moved into `tools/re` if wanted): `nre_reloc.py` (flatten the fake SELF with
RELATIVE relocations applied, name PLT stubs from aerolib), `nre_sites.py` (args at every call of a
function), `nre_refs.py` (calls, jumps, leas and pointers to an address), `nre_ctx.py` (callers,
vtables and singleton strings of a function).
