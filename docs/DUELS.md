# Team duels

Kaniel 2026-10-25: members of one co-op team can fight each other, started from the co-op menu (no
bell, no item in the world).

## How it plays

1. In the game, open the co-op team screen and press **Duel**. It is there while someone else of the
   team stands on your map and no duel is under way. The team screen opens with **F2** on the
   keyboard, with **L3+R3** then **Square** on a pad (the Insert menu, then its Co-op Team key), and
   from the game's System menu ("Co-op Team"). For 20 s after you get into the team's hunt, a hint
   on the left of the screen, under the party list, shows these keys and that duels are there.
2. Every other member on that map gets a yes/no box in the world: "<name> challenges you to a duel.
   Accept?". They have 30 s to answer.
3. Once everyone has answered (or the 30 s are up) and at least two are in, a 3 s countdown runs (3, 2,
   1 with a bar filling to the fight, then "Fight"), then the fight. It is free for all: everyone who
   accepted fights everyone else. Each game counts down on its own clock from the server's time left.
4. A duelist's lethal hit puts it **out**, not down. The game's own YOU DIED banner shows and the
   duelist stays standing at 1 HP and takes no damage until the duel ends. There is no party death,
   no wipe, no map reset, no echo loss, no bloodstain and no trip to a lamp.
5. The duel ends when one duelist stands (the winner sees ADVERSARY EXPUNGED), when a duelist presses
   **End the duel** on its team screen, when a duelist's game goes, or when a duelist leaves the
   duel's map (a load or a trip).
6. At the end every duelist gets back the HP it had when the duel started, every consumable it used
   (vials, bullets, knives, Molotovs, papers, anything in its inventory that is not a key item, back
   to its count from the start), and loses the poisons the duel put on it. Items picked up during the
   duel stay: the end never takes an item away. Everyone is friendly again.

Enemies keep playing and stay everyone's enemies, but the duel's rules don't apply to them. A lethal
hit from an enemy during the duel puts a duelist out too. Not yet seen in play: the enemies' AI does
not take the invaders' team as a target, so the enemies run by one duelist's game may leave its
foes alone while the duel lasts.

## How it works

- **The server keeps the duel** (`server/coop/duel.h`, `DuelReferee`, one per party, in memory only):
  who was asked and answered, each duelist's side, who is out, the stage and how it ended, and each
  member's wins while the server runs. Games send `DuelRequest` (propose, accept, decline, cancel,
  defeated); the server sends the whole `DuelState` to every member of the party on every change and
  after a hello. Protocol 8.
- **Hits are the game's own.** While a duelist fights, its game puts the bodies of its foes (as it
  shows them) on the invaders' team (21), as party death does with downed bodies. The game then lets
  the player lock on to them and hit them, and the struck duelist's own game applies the damage,
  flinch and all, as it does with an invader's hit. Each body's member is found from its network
  identity (its member's online id), else from where the vitals board says each member stands. An
  out duelist is no foe: its body gets its own team back.
- **The defeat** is party death's lethal check (`IsNoDead`'s exit hook): while the player fights a duel
  a lethal hit answers "no dead" and counts a duel catch. The duel tick then sets HP 1 and the
  no-damage flags, shows the YOU DIED banner (`SetTextEffect` 2, `TE_Died`) and sends DEFEATED. The
  winner's banner is `SetTextEffect` 7 (`TE_KillBlack`, "ADVERSARY EXPUNGED").
- **The give-back** keeps HP, every goods count outside the key items (`HeldItemGoods`) and the
  effects on it as the player joins the duel (the countdown). At the end it sets that HP, adds back
  what each goods count fell by (`DuelGoodsGiveBack`, silently), and takes
  off the poisons (`DuelAilmentEffects`: SpEffectParam stateInfo 2 and 5 rows that drain HP) that were
  not on before. A game away from the duel's map for 10 s gives back by itself, in case the server's
  end never comes.

## Later

Teams of two: the server's `Duelist.side` already carries a side per duelist (free for all gives
each its own); a team duel gives two duelists one side, and the games already only strike other
sides.

## Rig

Scenario `duel` (2 players): the host proposes from the menu, the guest accepts, the guest is put out
by the host's strikes (no down, no real death, no wipe, no lamp), its HP comes back at the end; a
second duel is cancelled from the menu. The guest's countdown logs 3, 2, 1 and the fight; it holds 10
knives and 3 Molotovs as the duel starts, 4 and none during it (taken out, standing for throws) and
picks up 5 pebbles, and holds 10, 3 and 5 after. Shots: guest-coop-hint, guest-duel-countdown. Verbs: `duel-propose`, `duel-answer yes|no`, `duel-cancel`,
`wait-duel <part>`, `duel-strike`.
