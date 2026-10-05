# Phase 1: persistent party and auto-rejoin

Goal: once two players form a party, the guest ends up back in the host's world after any
disconnect (death, boss kill, host death, area change) without anyone touching a bell.

## How a summon works today (shadp2p + shadnet-p2p, 01.09)

1. **Guest** rings the Small Resonant Bell, and the game POSTs `summon_messenger/create` (the sign).
2. **Host** rings the Beckoning Bell, and the game polls `summon_messenger/get` (search). The broker
   returns signs, which seamless mode allows from any map.
3. Host's game picks a sign and POSTs `summon_messenger/request` (claim). The broker stores the
   claim, plus the host's placement from the `X-ShadPS4-Bloodborne-Host-Placement` header.
4. Guest's next `create` poll receives the claim. The client hook warps the guest to the host's map
   (cross-map handoff), and the game joins the host's Matching2 room.
5. Guest POSTs `summon_messenger/delete` (consume). P2P signaling connects them, and the guest arrives.

Confirmed working on our build on 2026-10-05 (rig, two instances).

## Design

### Party identity: client → server header
- New client setting `SHADPS4_BLOODBORNE_PARTY=<name>:<password>`, later also an ini.
- The emulator adds `X-ShadPS4-Bloodborne-Party: <name>:<sha256(password)>` to every
  `summon_messenger/*` request. This follows the same pattern as the existing host-placement header
  in `http.cpp`'s `ApplyBloodborneSeamlessRoute`.

### Server: party-scoped matching (shadnet `SummonBroker`)
- `Advertise` records the sign's party key.
- `Search` from a party member returns **only** signs from the same party, so the host's
  Beckoning Bell always picks a party member. Without a party header, behaviour is unchanged.
- `Claim` rejects a claim across parties.
- A party record (members by UserId, current host, last host placement) outlives Matching2 rooms
  and summon TTLs, so it survives deaths and reloads.

### Client: auto-rejoin (shadp2p `bloodborne_re.cpp`)
- The fork can already dispatch a bell natively (`UseItemNativeApplyOffset`, Small Resonant Bell
  goods 205, used by the pre-match responder resume path).
- New `PartyController` state machine. While in a party and not in a session:
  - **guest** re-dispatches the Small Resonant Bell after it is back in control, i.e. after a
    reload or respawn finishes;
  - **host** keeps the Beckoning Bell active and re-dispatches it after its own reloads.
- Session-end detection: hook the multiplayer stop request (`MultiPlayStopRequestOffset`) and the
  Matching2 leave-room path that the RE trace already observes.
- Insight cost: we accept it in Phase 1 and remove it in Phase 3.

## Test plan (red → green)
1. Server unit tests in `server/tests/test_bloodborne_summon_broker.cpp`:
   - a party search returns only same-party signs;
   - a search without a party sees everything, as before;
   - a claim across parties gets `Conflict`.
2. Emulator unit tests for party header parsing and building (pure functions, no game).
3. Rig test: join, kill the guest, and the guest auto-returns. Then kill the host, and the guest
   is re-summoned after the host respawns.
