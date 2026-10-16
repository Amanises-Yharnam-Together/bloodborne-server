# Agent A: character channels (players + enemies)

Addresses are eboot_rel.img offsets (image base 0). "Guess" marks an inference I did not prove.

## Helpers found (missing from send_sites/recv_sites because they tail-jump)
| addr | what |
|---|---|
| 0x017893E0 | RECV_FROM_HOST(sess, ch, buf, max): only while SprjSessionManager(0x05540290)+0x124 is 3 or 6; reads from the member id at SessionManager+0xD0 |
| 0x01789A40 | SEND_TO_HOST(sess, ch, buf, size): to the member at +0xD0 |
| 0x01789980 | SEND_ALL(sess, ch, buf, size): every member but me. Nothing references it (dead code) |

Notes:
- +0x124: 3 = hosting, 6 = summoned guest.
- +0xD0 is the session owner, the host. The repo comment at bloodborne_re_internal.h:499 calls it "the local one". Sending to yourself would make no sense, so that comment looks wrong.
- Other RECV_FROM_HOST users: ch 7, 0x19, 0x11 (0x6400), 0x10 (0x800), 0x27, 0x32, 0x33, 0x3F.
- Other SEND_TO_HOST users: ch 0x15, 0x16, 0x2C.

## Summary table
| ch | rel? | sender -> receiver | trigger | payload | effect at receiver | evidence |
|---|---|---|---|---|---|---|
| **6** | unrel. | every player -> every other member (peer to peer, no relay) | local player's net ctrl update, every few frames (countdown ctrl+0xC8) | Header byte = number of TLV blocks. Each block starts with `u16 (count<<8)\|type`. Types: 0 = core 0x18 B; 1 = core 0x20 B; 2 = u16 behavior events; 3 = (u16 id,u8) bool vars; 4 = (u16,u16) vars; 5 = (u16,u32) int vars; 6 = (u16,float) float vars; 7 = 0x14-B records; 8 = u16 list; 9 = 1 flag byte. Core 0x18 B: pos vec3 f32, u32 state (0x0150A8E0, guess: move state), u16 HP, u8 HP%, 2 x u16 heading angles | Remote PlayerIns net ctrl steers position (interpolates toward the target) and heading. It feeds the events and variables into the remote's Havok behavior graph, which plays the animations. **This is player position, rotation, animation and HP sync.** | send 0x01529470 -> 0x01A1E270 (collect behavior) -> 0x01A72220 (serialise, SEND at 0x01A731C5); recv 0x015247F0 -> 0x01A6D390 (RECV at 0x01A6D449, TLV jump table 0x01A6D55C) |
| 0x14 | unrel. | local attacker -> victim player's member (PlayerIns+0x3D8) | the local player hits a remote player (0x01A33440, damage-module vtable slot at 0x05397C70) | 0xEC B whole hit: attack vector, scaled damage components (+0x10..+0x16 x rates), attack/param ids, 0x98 B of attack data, flags | The victim's game replays the hit on its own player: 0x01A341B0 from the player update 0x018F4FF0 (RECV at 0x01A34356). The victim's own game computes the victim's HP | send 0x01A33797 |
| **0x15** | unrel. | guest -> host (SEND_TO_HOST) | the guest's hit on an enemy (damage-module vtable+0x60 = 0x01A29E40; also 0x01A35A00) | 0xC B: u32 victim chr id (+0x90), s16 damage (hit+0x22C, clamped), s16 hit+0x230 (stamina/poise damage, guess), s16 hit+0x234 | The host applies it to its copy: HP = HP - dmg via 0x018BCD40. Data+0x134 (stamina/poise, guess) -= v2, floored at -50 and capped at +0x138. If HP <= 0, kill via 0x018BF9A0 | send 0x01A2A08E; recv loop 0x01916562/0x0191670E in 0x01915320 |
| 0x16 | rel. | guest -> host | timer (+0x638) | u32 n + n x u32 chr ids (list NetWorldChrSync +0x610) | The host stores the list per player (0x018F0B70 / 0x01A6C590). Guess: "enemies near me" | 0x018F0780 (send 0x018F0B2A, recv 0x018F0923) |
| 0x17 | rel. | host -> each player's member | NetWorldChrSync host branch | u32 n + n x u32 chr ids | The guest sets the per-block u16 table to 0xFFFF, then marks the listed ids as 1. Guess: which enemies the host runs for you (answer to 0x16) | send 0x018ED3BC, recv 0x018ED07E |
| **4** | unrel. | the enemy's **owner** (ownership word >= 0, normally the host) -> all | timer, round-robin over the current block, up to 8 per packet, only entries with dirty bit 2 | 8 x 12 B {u32 chr id, 8 B packed pos relative to the block origin (encode 0x0178FA90 / decode 0x0178FCD0)} | Writes the net position record (+0x20) and sets flag 1. The network controller moves the enemy to it | send 0x018EEA20, recv 0x018EEB45 |
| **5** | unrel. | **host only** (state 3) -> all | every 4th frame round-robin, plus at once on dirty bit 0x20 | 10 x {u32 chr id, u32 = u16 HP (data+0xF8, clamped 0xFFFF) \| u16 flags ([chr+0x18]+0x20 bit1 \| chr+0x348)} | The guest (state 6) writes the +0x30 word and sets flag 0x10. The consumer reads it and clears the flag (accessor 0x018F0680). **Enemy HP state, host-authoritative** | dirty set 0x01916D3A; send 0x018EF4DE/0x018EF934, recv 0x018EF742 |
| 0x29 | rel. | host -> each guest in list +0x690 | one enemy per frame, round-robin | 0x6C B: 12 B relative pos (+rotation via 0x0178FA90) at +0, 0x60-B state record at +8, u16 chr id at +0x68 | The guest sets the enemy transform directly (pos and +0x1D0 rotation) and copies the 0x60 record. Catch-up snapshot (guess) | send 0x018EDB92, recv 0x018ED843 |
| 0x36 | rel. | host -> guests | for newly added players (list +0x6B0) | 8 B, type in the low 2 bits: 2 = clock (sceRtc ms, 30 bits); 1 = header {count:14, chr id:16}; 0 = anim entry {norm. time:13, anim id:17} | Clock: sets chr +0x318/+0x31C on chrs of kind 4. Anim: plays the anim at that time (0x018C68B0). **Join-time enemy animation snapshot** | send 0x018EE150/0x018EE429/0x018EE49D, recv 0x018EDEB0 |
| 0x3D | rel. | anyone -> all | event queue NetWorldChrSync +0x2080 | n x 0x64 {u32 chr id, 0x60 state record} | Writes the 0x60 record and sets flag 4. Record meaning unknown; guess: AI/action state | send 0x018EEF31, recv 0x018EF0FB |
| 0x41 | rel. | anyone -> all | event queue +0x26A0 | n x 0x18 {u32 chr id, 0x14 record} | Writes the +0x40 record and sets flag 0x40. Meaning unknown | send 0x018EFB41, recv 0x018EFD05 |
| 0x34 | rel. | 0x018BE0A0 -> one member (rsi = member) | called from 0x018E5FD0 / 0x018F7FB0 | 0x64: u32 chr id + 3 x 0x20 B | Copied into chr +0x218..+0x277, skipped for chrs of kind 4. Meaning unknown | recv 0x018EFF42/0x018F00EE |
| 0x24 | unrel. | anyone -> all | ChrIns force-animation vtable method 0x018C6DB0 (+ player update) | 8 B: 17-bit chr ref (low 16 bits of chr id, or 0x10000 \| player +0x3B8), 17-bit anim id, ms timestamp in the upper bits | Resolves the chr and plays the anim (0x018C68B0). Event-driven animation sync for both enemies and players | send 0x018C72C0, recv 0x019170D9 |
| 0x25 | unrel. | local -> a specific remote player's member | PlayerIns vtable method 0x01904BD0 | s32 anim id | The victim's game makes its own player play it (0x017E28D0) and then broadcasts ch 0x24. Used to force an anim on another player, e.g. a grab | recv 0x018F5C73 (player update) |
| 0x26 | unrel. | local -> remote player's member | PlayerIns vtable 0x018FC1E0 (kill reward) | s32 amount | The receiver adds it via vtable+0x3D0. Guess: blood echoes for a kill | recv 0x018F5EBE |
| 0x12 | unrel. | anyone -> all | throw/visceral manager (0x0153BB60 from 0x01915320) | 0x2C: pos+heading of thrower and of victim, u16 net idx x2 (0x0191BAA0), u32 throw id, u8 victim dead | Throw start sync (ChrModules+0x88 = throw module) | recv 0x0153BF10 |
| 0x37 | unrel. | anyone -> all | 0x0153D490 | 4 B {u16 idx A, u16 idx B} of a throw pair | Throw event (guess: end/escape) | recv 0x0153BF10 |
| 0x30 | unrel. | anyone -> all | 0x0153D740, from the throw module 0x01A5FEB0 | 4 B | Throw event (guess) | recv 0x0153BF10 |
| 0x43 | unrel. | player -> one member (on request, 0x01904AE0 from 0x01783160) or all (0x01A6CB30, after a load) | load / request | 0x90 B = PlayerIns+0x470 block (u32 array + bitmask at +0x90) | Copied into the remote's block and +0x95 set. Meaning unknown; guess: per-player visible state | recv 0x01A6CC40 (from 0x018F3A10) |

## Answers

**1. Remote player movement.**
- It travels on **channel 6** (unreliable). Each player sends its own state straight to every other member; nothing goes through the host.
- Character controller factory 0x018C0280(chr, kind):
  - kind 1 = local-player net-sender controller (vtable 0x05370430, update 0x01529470).
  - kind 2 = remote-player network controller (vtable 0x05370250, update 0x015247F0).
  - The PlayerIns ctor 0x018F1700 passes the kind. PlayerIns+0x3D8 is the member id.
- Sender data sources:
  - Transform is at ChrModules(+0x3B0)+0x68: position at +0x1E0, heading at +0x1D0/+0x1D4.
  - HP is at data module +0x20: +0xF8 HP, +0xFC max HP.
  - Animation is Havok behavior sync, not animation ids. ChrModules+0x30 is the behavior module (+0x384 = net sync on). Its events and variables are collected through SprjHkBehManager 0x01DE04E0.
- There is no channel 0-3 sender. Channels 0-3 are not used for players.
- Extra animation paths:
  - ch 0x24: forced animations, sent to all.
  - ch 0x25: forced animation request sent to one victim.
  - ch 0x12/0x37/0x30: throws (visceral attacks).

**2. Damage and HP.**
- Whoever is hit has its HP decided by the game that owns it:
  - **Player victim:** the attacker's game sends the whole hit (ch 0x14, 0xEC B) to the victim's game. The victim's game applies it to its own player. That HP then reaches everyone through ch 6 (u16 HP + HP%).
  - **Enemy victim, guest attacker:** the guest computes the damage and sends {victim id, damage, poise/stamina damage} to the host on ch 0x15. The host applies it to its copy and does the kill.
  - **Enemy victim, host attacker:** the host's own hit applies directly; nothing is sent (0x01A29E40: state 3 -> flag 0).
- A guest still runs the hit locally first (vtable+0x68, with arg cl=0 for the guest path). Guess: that gives reactions only, while HP is taken from the host.
- Enemy HP state then flows host -> guests on ch 5 (u16 HP + u16 flags), written to the NetWorldChrSync +0x30 word.
- Kill echoes for a killed player: ch 0x26 (guess).

**3. Enemy position, animation and AI.**
- The NetWorldChrSync table is at WorldChrMan(0x0553E878)+0xA40. Its update is 0x01915320, with body 0x018ECEB0.
- Per block (chr id bits 11..15 = block, bits 0..10 = slot), the table holds:
  - +0x18: s16 ownership words. >= 0 means this machine runs the AI; < 0 means the network controller drives it.
  - +0x20: position records.
  - +0x28: 0x60-B state records.
  - +0x30: HP words.
  - +0x38: flag words.
  - +0x40: 0x14-B records.
- How it travels:
  - **Position:** ch 4, sent by the enemy's owner, which is the host by default (guess, but ch 5/0x29/0x36 are hard-coded host-only).
  - **HP:** ch 5, host only.
  - **Full snapshot:** ch 0x29 (pos + rotation + 0x60 record), host -> guest, round-robin.
  - **Animation state at join:** ch 0x36, host -> new guest.
  - **Animations during play:** ch 0x24, any member.
  - **0x60/0x14 records:** event-driven on ch 0x3D/0x41, from anyone. Likely AI/action state; not proven.
- No AI decisions are sent. The non-owner's network controller just follows these records.
