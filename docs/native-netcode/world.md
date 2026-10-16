# Agent B: world state / events / objects channels (static RE, 1.09 eboot, image base 0)

Status: complete for the assigned channels; guesses are marked (guess).

Helpers: NET_SEND 0x017894E0, NET_RECV 0x01789200 (any member), NET_RECV_FROM_HOST 0x017893E0
(session, ch, buf, max): pops only from the member id at SprjSessionManager(0x05540290)+0xD0, and only
while CSMultiPlayMan state +0x124 is 6 (summoned guest) or 3. Most senders below loop over all members
skipping "me" (0x00CA1670) = broadcast.

## Summary table

| ch | size | sender / when | payload | receiver does | matters |
|---|---|---|---|---|---|
| 0x0E | 0x14 | any player; Lua/event "LuaCallStart(+Plus)" 0x0132E960/0x0132EAD0 (type 0x0C), "CustomLuaCallStart(+Plus)" 0x0132ED40/0x0132EEB0 (type 0x36), generic 0x0133A1F0; only when the event proxy +0x8B (sync) is set | {u32 type, u32 event id, u32 arg, u32 arg2, u32 sender player no (GameDataMan[+8]+0x10)} | 0x01339B50 (event update, from 0x01312FF0) drains all members, runs 0x0139A9D0(eventsys+0x38, type, id, arg, arg2, remote=1, sender) = same Lua event call locally, then notifies Lua listeners ([0x0553B0C8] list, vtbl+0x18) | yes: scripted events (deaths 4044/4063, boss etc.) |
| 0x0F | 4 | any player; Lua `CompleteEvent(flag)` 0x0132AAD0: sets the flag bit locally, then broadcasts | {s32 event flag id} | 0x01339B50: raw-sets that flag bit ON in EventFlagMan (no callbacks), if its block is loaded | yes: flag ON only, never OFF |
| 0x10 | 0x10+compressed (recv max 0x800) | HOST only (state 3), once, to the newly joined member: session event handler 0x0178B5E0 case 0x0E "member joined" -> 0x01786050 | see "event flag snapshot" | guest, while net-sync state [0x0553D6D0]+0x84 == 1 and after 0x11 arrived: inflate, 0x013BECA0(EventFlagMan, buf) overwrites flag blocks; then +0x84 = 2 and 0x01317780 (OnBeJoinStart_* Lua) | YES: join handshake + host world flags |
| 0x11 | var (recv max 0x6400) | HOST only, same place, right after 0x10 send (0x01786360) | 3 length-prefixed blobs: "LCED" decals, "OBJ " map objects, "CHR " characters | guest (state +0x84==1) copies into GlobalState(0x05556678) +0x16C8/+0x16C0 (decals), +0x16A8/+0x16A0 (objects), +0x1688/+0x1684 (chrs); applied on the next map load | YES: host's dead enemies + object states for a joining guest |
| 0x22 | 4 | a player answering a generic dialog (menu 0x01710A30 / 0x01711A60, vtable 0x05381CC8) sends its answer to one member (by slot) | {u32 answer} | 0x01780960 stores answer[slot] in session +0xA8 array; event condition 0x01306D30 tallies answers once all slots answered (0x01780A70) | minor (multiplayer dialog votes) |
| 0x27 | 1 | HOST: 0x0178DD50 from 0x015BE2A0 when a phantom of type 1 leaves with reason 0 (boss beaten "thanks") ; also runs CustomLuaCallStart(4063) and LuaCallStartPlus(4044,2,id) | {u8 0} | guest 0x01789B60 (session update): adds pending echoes ([0x0553B188]) to PlayerGameData +0x94/+0x98, Lua "OnBeThxKickOut", 0x014EB860 (go home) | session end on boss kill (kick) |
| 0x2A | <=0x64 | any player: map-object manager 0x015B3C30 flushes the per-block pending list (+0x180) | {u8 0,u8 0,u16 n,u32 map id, u16 object index[n]} | 0x015B82B0: 0x015A66A0(object) per index = break/destroy object (guess: breakables) | world cosmetics/breakables |
| 0x2B | 0x20 | any player: 0x015B3790 flushes per-block anim change queue (+0x150) | {u8 0,u8 0,u16 n(<=3),u32 map id, n x {u32 objIdx:12 | animTime:19 | flag:1, u32 anim id}} | 0x015B82B0 sets object anim ctrl (+0x2B0): id, time | object animations (lifts/doors) |
| 0x2D | 8 | the player who triggered an ObjAct (lever/door/chest), 0x013AE600, if local actor (+0x7A) and not replaying a remote act | {u32 actor (0x80000000 | player net id +0x3B8, or -1), u8 block&0x3F, u8 new state, s16 objact index} | 0x013B2790 (via 0x013B2F90): 0x013AF830 replays the act on the receiver with that actor; summoned guest also refreshes its correction timer to 10 s | YES: levers/doors |
| 0x2E | 6 | any: 0x013AFC30/0x013B03F0/0x013B0BB0 (event-driven objact enable/disable) | {u8 block, u8 0, s16 objact index, u8 state(+0x5C), pad} | 0x013B2790: objact +0x5C = state | yes (objact enable state) |
| 0x2F | 4 | HOST only (state 3) on every ObjAct execute (0x013AE600); also 0x013B3890 | {s8 block, u8 state (vtbl+0x38), s16 objact index} | only summoned guest (state 6): queue entry (+0x3FC8) with 10 s timer; when it expires and local state != host's: reset + 0x013AF540 force state | YES: host-authoritative door/lever state |
| 0x31 | 0x18 / 0x1C (recv max 0x100) | any player running an EMEVD event instance flagged for sync: kind 1 by 0x012EE540 when the instance restarts (from its update 0x012ECDC0, sets +0x60 = 10000 first); kind 2 by 0x012EE660 from an EMEVD instruction handler (0x017C75B0) | {u16 0, u16 kind, u32 body len (8 or 0xC), u32 event id (+0x28), u16 slot (+0x2C), u16 (+0x2E)>>1, body}. kind 1 body {u32 seq (+0x48), u32 value ([+0x20])}; kind 2 body {u32 target (+0x60; 10000 = 'the player' becomes 10001 + sender's net id +0x3B8), u32 seq+1, u32 value} | 0x012F0F90 (SprjEmkSystem update) drains all members, matches the running instance by (event id, slot) and calls its vtbl+0x28 = 0x012EE420: kind 2 -> +0x60 target, +0x4C, [+0x20] = value; kind 1 (if seq matches) -> target = 10001 + net id of the SENDER's character, +0x4C, [+0x20]. Then the event's own instructions run on every machine (the repo's 'remote set flag' 0x017C0570 and 'end event' 0x017C0901 are EMEVD bank handlers inside 0x017C0420 acting on such a remotely-driven instance, not packets of their own) | YES: how one player's trigger (boss fog, talk, lever events) drives the same event on the others |
| 0x32 | 0x10 | host: 0x015BD0E0/0x015BD270, summon manager 0x01872360 | {u32 chr handle, u32, u32, u8 phantom type, u8 flag} | guest 0x01947310: CSMultiPlayMan 0x01E54C30 + join message 0x015BC590 | announcements |
| 0x33 | 8 | any: 0x015BE2A0 (player/phantom left: message + CSMultiPlayMan 0x01E55050), 0x015BBC00 | {u32 chr handle, u32 reason} | guest 0x01947310 -> 0x015BE770 (leave message/cleanup) | announcements |

| 0x38 | 4 | host, or a player whose HP is 0, or a guest: object class with vtable 0x0534BFD0 (0x01CF98C0, per character group at +0xD0) when its countdown (+0x144) runs out | {u32 id (first member's +0xD8)} | same function: a matching id resets the local countdown | (guess) a shared AI/character timer; low |
| 0x39 | 2 | 0x01A249D0 (hit/kill code, vtable 0x05335170) unicast kind 0 to the hit player's member; MapItemMan 0x017D2F30 replies kind 1; 0x017DE500 unicast | {u8 kind, u8 n} | 0x017D2F30: kind 0 -> subtract min(n, have) from PlayerGameData +0x84 (0..99) and reply kind 1 with the amount (queued while GlobalState+0x1490 != -1); kind 1 -> add n (capped 99), HUD refresh | (guess) Insight transfer between players; low for co-op |
| 0x2C | 8/0x68/0x9C (recv 0x100) | MapItemMan 0x017D2F30, 0x017DE6E0, 0x017DE870 | header u32 1 + message kind (see repo MapItemPacketChannel) | 0x017D33D8 (host's item sync clears guest map items) | already handled by the repo |
| 0x3E | var <= 0x2710 | any player, every frame with new bullets: SprjBulletManager ([0x0553E8D0]) update 0x01A011A0 serializes its pending list (+0x80..+0x88) through the serializer at +0x70 | packed bullet-create records | same fn: 0x01A05B70 parses, 0x01A03B50 spawns the remote bullets | YES (projectiles, incl. thrown/spell bullets) |
| 0x3F | var (recv max 0x2710) | HOST once to a newly joined member: 0x01A04190 from the join handler 0x0178B5E0 (after 0x10/0x11) | {u8 count, records} snapshot of live bullets | guest 0x01A011A0 (recv from host) recreates them | join only |
| 0x40 | var <= 0x2710 | any player: bullet manager list +0xE0..+0xE8 (flushed each frame) | {u8 count, 16-byte records...} | 0x01A011A0 -> 0x01A06380 per record | (guess) bullet hit/expire updates |

## Event flag snapshot (channel 0x10) details
Sender 0x01786050(session, member): requires WorldChrMan+0x60 (player) and [0x0553B148]. Buffer size =
EventFlagMan+0x20 (block bytes) * 12 + 0xA4. 0x013BE3C0 serializes:
- inner header "INFE" (0x45464E49), u16 version 1, +8 = 0xC (descriptor size), +0xC = 0x14 (table offset), +0x10 = 0xA4 (data offset)
- 12 descriptors {u32 block index, u32 data offset, u32 block size} at +0x14, data at +0xA4, filled by 0x013BEAC0(flagman, digit, area, block, group): memcpy of one flag block.
- blocks: global flags (digit 0, groups 0,1,5,9 = flags 0xxx,1xxx,5xxx,9xxx), the sender's CURRENT map
  (area/block from player +0x3F8) map flags 1AAB g xxx for groups 0,1,4,5,6,7 and 6AAB g xxx groups 0,1
  (if the map is not in the 17-map table 0x047314D0, area 99 block 0 = chalice-dungeon flags instead).
Then deflate level 9 ("algorithm=deflate;level=9"), outer header {"MNSE" (0x45534E4D), u32 1, u32 compressed size, u32 raw size}, payload at +0x10.
Receiver (0x01783160 tail, 0x017850B9): only when state +0x124 == 6 and net-sync +0x84 == 1 and GlobalState+0x16A0 > 0 (0x11 already in); checks magic/version, inflates into a buffer of raw size, 0x013BECA0 applies, sets +0x84 = 2.
So: the native game syncs flags host->guest ONCE at join, only for the host's current map + global groups;
afterwards only Lua CompleteEvent (0x0F, ON only), remote Lua event calls (0x0E) and the EmkSystem messages carry flag changes.

## Channel 0x11 blob details (0x01786360)
1. u32 len, "LCED" + 0x15020300: WorldDecalMan [0x0553E928]: per block (count +0x10, 0x6E40-byte entries at +0x18,
   decal list at +0x6D80): {u32 map id, u32 (n*16|0xC), u32 n} + n x 16-byte decal (node +0x14); end {-1,0,0}. Consumer 0x01A84940.
2. u32 len, map objects [0x0553B168] (0x1F8 per block, 0x2E0-byte objects), serialized by 0x015B9CB0 (magic "OBJ " 0x4F424A20);
   per block a bitfield ((bits+7)/8 + 0xC). Consumer 0x015B7EA0 (reads +0x16A0/+0x16A8 when not in own world, GlobalState+0x1592 == 0, else +0x16B0/+0x16B8).
3. u32 len, " RHC" (0x43485220) + 0x15091700: WorldChrMan +0x278 blocks (0x18 each at +0x290), each via 0x0190E750 =
   per-block character bitfield (bits at block+0xB8) -> very likely the "dead/killed enemies" state. Consumer 0x0191BDD0 (from 0x01939970).
The same serializers are used by 0x0131E5C0 and 0x0156AA50 (likely save/load), so these are the world-state
records a load reads; the guest loads the host's map with the host's records.

## Answers to the specific questions
- Event flags: there is NO general flag replication. (1) Join: host -> new guest, once, channel 0x10 = the
  host's global flag groups (0xxx,1xxx,5xxx,9xxx) + the host's CURRENT map's 1AABgxxx (g = 0,1,4,5,6,7) and
  6AABgxxx (g = 0,1) blocks, raw overwrite on the guest. (2) Lua `CompleteEvent(flag)` on any machine ->
  channel 0x0F to all, receiver sets the bit ON (never OFF). (3) Everything else follows because every machine
  runs the same EMEVD/Lua events and they are kept in step by channel 0x31 (event instance target/value) and
  0x0E (remote Lua event calls). Host -> guest only for 0x10; 0x0E/0x0F/0x31 go both ways (any member).
- 0x10 snapshot: sent once by the host from the session event handler 0x0178B5E0, case event type 0x0E
  ("member joined", when the member is not yet in the +0x68 list and our state +0x124 == 3), in the order
  ch 7, 0x19, 0x10 (0x01786050), 0x11 (0x01786360), 0x3F (bullets), 0x42. The guest only accepts it while its
  net-sync state +0x84 == 1 and after the 0x11 blobs arrived; applying it moves +0x84 to 2 (join continues,
  OnBeJoinStart_* Lua). A later map change is NOT re-snapshotted by this path.
- 0x11 (WorldDecalMan): decals (blood splats etc.) + map object state bitfields + per-block character bitfields
  (very likely dead enemies) of the host world, stored in GlobalState and consumed when the guest loads the map
  (0x01A84940 decals, 0x015B7EA0 objects, 0x0191BDD0 characters).
- Levers/doors/ObjAct: the user's game sends 0x2D (act start, with the actor's net id) to all and each receiver
  plays the act with that remote character; the host additionally sends 0x2F (resulting state) and a summoned
  guest forces that state after 10 s if it still differs; 0x2E sets an objact's enable state (event driven).
  Breakables/animated map objects: 0x2A (break list) and 0x2B (anim id + time), any player.
- Boss/fog: no dedicated boss-fog packet. A fog/boss trigger is an EMEVD event; when a remote player is the
  trigger the instance's target (+0x60) arrives via 0x31 (10001 + net id), so the event runs everywhere.
  Multiplayer boss/death scripts use Lua events 4044/4063 sent on 0x0E (0x0178DD50 calls
  CustomLuaCallStart(4063, id) and LuaCallStartPlus(4044, 2, id)). On a boss kill the host sends 0x27
  ("thanks" kick-out: guest receives pending echoes, OnBeThxKickOut, goes home) to each type-1 phantom;
  0x32/0x33 are the join/leave announcements (CSMultiPlayMan + system message).
- 0x22 is a multiplayer dialog vote; 0x38 an unidentified shared timer; 0x39 a 0..99 counter transfer (guess Insight).

## Co-op relevance (for a party of port players)
- Highest: 0x10/0x11 (what a joining guest sees: flags, dead enemies, objects), 0x31 + 0x0E + 0x0F (events
  driven by the other player), 0x2D/0x2F (doors/levers), 0x3E/0x40 (projectiles).
- Note 0x10 contains only the host's current map: flags of other maps never reach the guest natively.
- 0x2F corrections and 0x10/0x11/0x3F/0x27 are host-only (state +0x124 == 3) and guest-only on receive (6).
