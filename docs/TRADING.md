# Trading between party members

Party members give each other Blood Echoes, Blood Vials and Quicksilver Bullets, and may ask for
some back (a swap). The server owns every trade; each game owns its own inventory. Port only,
behind `SHADPS4_BLOODBORNE_TRADE=1` (with the server world, `SHADPS4_BLOODBORNE_SERVER_WORLD`),
until the rig and a playtest pass.

## How a trade goes

1. The giver picks "Give" on a Blood Vial or Quicksilver Bullet in the inventory (or "Give Blood
   Echoes" there, shown while it has any), and a number in the game's quantity picker (cursor on
   No; down from 1 wraps to all of them). The offer goes to the nearest other party member
   standing on the same map. The game's inventory hides vials and bullets (they are on the HUD
   only); with trading on, the port lists them in the Consumables tab (row filter 0x01BD8150 made
   to keep every row), with the game's own Use and Discard too.
2. The giver's game takes the items out of its inventory at once, then sends `TradeOffer`.
3. shadNet checks it (both members online in the party, tradeable items, counts within limits, at
   most 4 open offers per member) and asks the other member (`TradeAsk`), or gives the items
   straight back (refused).
4. The other member's game shows the game's yes/no box: "Ada offers you 4 Blood Vials. Accept?"
   (the cursor starts on No). To accept a swap its game takes out what was asked of it first.
5. shadNet hands each side its part (`TradeGrant`); declined, unanswered for 60 s, or withdrawn,
   the offer goes back to the giver. A game adds a grant like a pickup (up to the max held, the
   rest to the storage box, with the game's own "item obtained" popup), adds echoes through
   AddSouls, and acknowledges it (`TradeGranted`).
6. The giver sees what became of its offer in the game's OK box.

shadNet keeps open offers and unacknowledged grants on disk (`trades.pb` in the party's world
folder, written whole on every change) and hands them again when a member's game says hello, so a
crash or a restart of either side loses nothing. A grant is added once per game run.

Known limits: a giver whose game crashes after taking items out but before its offer reached the
server (one stream tick, 20 ms) loses them if the game saved in between. The quantity picker steps one at a time, slow for large
echo amounts (the game's LargeNumSelect is not traced yet). An offer's box opens in the world
whenever no menu is open, also mid-fight; its cursor starts on No.

## Code

- `server/coop/trade.{h,cpp}`: the shared part. `TradeDesk` (shadNet's per-party trades),
  `TradeClient` (a game's offers, asks and grants), the tradeable items and the game's words.
- `server/coop/coop.proto`: `TradeOffer`, `TradeStatus`, `TradeAsk`, `TradeAnswer`, `TradeCancel`,
  `TradeClosed`, `TradeGrant`, `TradeGranted`, `StoredTrades`; `Welcome.trades`.
- `server/src/bloodborne_world.cpp`: the desk per party world, its file, expiry on the tick.
- `emulator/src/core/bloodborne_trade.cpp`: the inventory (take out, put in), the client.
- `bbport/gpu/shim/bbport_trade_menu.cpp` + `bbport/gpu/bbcoop/trade.cpp`: the menus.
- Rig: scenarios `trade` (headless) and `trade-menus` (drawn, screenshots); verbs `trade-stock`,
  `trade-give`, `trade-offer`, `trade-answer`, `expect-held`, `wait-trade`.

## The game's functions (1.09, static RE 2026-10-14)

| What | Where |
|---|---|
| PlayerGameData | `[0x0553B130] + 0x08`; +0x94 Blood Echoes, +0x328 inventory, +0x1D0 EquipGameData |
| Find an item's inventory index | `0x014D9F60(inventory, &item id)`; entries 16 bytes {handle, item id, count} |
| Change a count | `0x014D0BE0(EquipGameData, index, delta, force, no_replace, allow 111)`; clamp first: below 0 turns into the max |
| Give like a pickup | `0x017D89F0(MapItemMan [0x0553D6E0], {n, {handle 0xB0000000\|id, item 0x40000000\|id, count, -1}[n]})` |
| Add echoes | AddSouls `0x018FC600` (player vtable +0x3D0) |
| Item commands list | `call 0x01BEA630` at `0x01BA152A` (factory vtable 0x0533FE00, item id at factory +8 +0x4C) |
| Add a row | `0x01B4C9A0(model, name+help texts, Function*, 0)` |
| Quantity picker | `0x01C203C0(Job**, owner, L"NumSelect", text, min, max, on_ok, extra)` |
| Yes/no, OK box | `0x01C1ED10`, `0x01C1F500` (Job**, owner, text); answer MenuMan `[0x05562878]` +0x1CC (1 yes, 2 no) |
| Show in the world | root `0x0176BAB0()`, owner root+0x48, push `0x01BB27A0(Handle*, root, Job**, abort)`; not while MenuMan +0x2FC; the box is gone once ours is the job's last reference |
| Inventory row filter | `0x01BD8150` leaves out goods 900, 901, 1000; with trading on it is `mov al, 1; ret` |

Goods: Blood Vial 1000, Quicksilver Bullets 900 (max held 20, storage 600). Other consumables that
could follow: Molotov 1200, Throwing Knife 1240, Pebble 1230, Antidote 1100, Sedative 1101,
Numbing Mist 1270, Blood Stone Shard 3000, Twin Shards 3010, Chunk 3020, Blood Rock 3030.
