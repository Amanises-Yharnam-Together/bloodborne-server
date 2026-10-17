# SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later
"""The rig's test scenarios (run_scenario.py): each role's autoplay script, the lines its log
must show ("expect"), the lines it must not show ("forbid") and the known problems it may show
("allow").

Every run starts from the baseline saves (run_scenario.py --save-baseline): every game at the first
Central Yharnam lamp. Every script starts with START (travel there unless already there).

Commands (see AutoplayVerbs in bloodborne_autoplay.cpp): wait <s>, wait-session <n> [<timeout>],
wait-members <n> [<timeout>] (party members on this map), wait-map <map> [<timeout>],
travel <warp point> [<timeout>], ensure-map <map> <warp point> [<timeout>] (travel unless
there already), hurt <nearest|handle> <hp>, kill <nearest|self|handle>,
teleport <x> <y> <z> [<heading>], approach <target> [<dx> [<dz> [<heading>]]], mark <target> (later "marked"),
attack <target> [<timeout>] (1 HP left, then light attacks from each side until it dies),
hit <target> [<timeout>] (light attacks until one lands),
press <button> [<ms>], stick <x> <y> [<ms>], pickup [<meters>] [<timeout>] (onto the
nearest usable map item, then X until it is gone), snapshot, log <text>,
expect-hp <handle|self> <op> <value>, expect-session <n>, expect-map <map>,
set-flag <flag> <0|1>, expect-flag <flag> <0|1>, wait-flag <flag> <0|1> [<timeout>],
wait-solo [<timeout>] (until the session is gone), note-max-hp (the player's, for
expect-max-hp <target> <op> <value|noted>), expect-effect <SpEffect> <0|1>,
expect-others-near <n> <meters> (n other players alive that close), expect-compass <bearing> <metres>
[<degrees> [<metres>]] [down|dead] (within 10 s a marker of the teammate compass at that bearing,
degrees right of straight ahead, and distance; 20 degrees and 1.5 m either way by default; down or
dead: the marker shows that), expect-others-shown <n> (exactly n other players
shown, not hidden for another map), watch-unseen <handle> <seconds> (from now on, every frame the
character is drawn logs "watched <handle> drawn"; forbid that), godmode [off] (the player's HP kept at max), done. Targets: nearest, self or a handle in hex; kill
uses the game's own kill (as an event's ForceCharacterDeath), so bosses die in one step.
Party death (the scenario's "settings" switch it on): rise <metres> (straight up, to fall),
approach-downed (beside the nearest downed member on the map), revive-downed [<timeout>]
[r1|r2] (R1, or R2, on the downed member until nobody on the map is down), wait-vital <up|down|dead>
[<timeout>] (this player's state), wait-member-vital <up|down|dead> [<timeout>] (another
member's, on the vitals board). shot <name>: the port's picture of its next frame, overlays and
all (the party HUD), kept in the run's folder as <role>-<name>.png; the emulator skips it.
"""

HUNTERS_DREAM = 2102962
CENTRAL_YHARNAM = "0x18010000"
CENTRAL_YHARNAM_LAMP = 2412950  # the first lamp, where the rig's characters start
CENTRAL_YHARNAM_LAMP_2 = 2412951  # talk 241001, 3 m from talk 241319 (Gilbert's window?)
CATHEDRAL_WARD = "0x18000000"
CATHEDRAL_WARD_LAMP = 2402950
ENEMY = "0x1001400b"  # 484 HP, 17 m from the lamp (farther ones are not awake)
BY_THE_LAMP = "0x100140a0"  # 999 HP, 1.6 m from the first lamp: by Gilbert's window
AWAY_FROM_LAMP = "-215.0 -54.5 178.0"  # beside that enemy, 17 m from the lamp
WARD_ENEMY = "0x10010019"  # 211 HP, 7 m from the Cathedral Ward lamp (the nearest, 0x10010075,
# has 999 HP and dies of nothing: an object, as Central Yharnam's 0x100140a0)
# Another one, 22 m from that lamp (123 HP): lone-guest's, as WARD_ENEMY's kill stands from
# ward-walk-kill on (Central Yharnam and the Ward load together, so it never comes back).
WARD_ENEMY_2 = "0x10010021"
CLERIC_BEAST = "0x1001409f"  # entity 2410800; its death runs event 12411700
CLERIC_BEAST_DEFEATED = 2411  # set by event 12411700 after Handle Boss Defeat
GASCOIGNE = "0x1001409c"  # entity 2410810 (his beast form: 0x1001409e, 2410811)
GASCOIGNE_BEAST = "0x1001409e"
BIG_BY_THE_LAMP = "0x10014001"  # boss-sized (max HP 1917), near the first lamp
# Event 12411800's own flag: on, the map's load puts Gascoigne down (Force Character Death);
# off, he waits for the fight. Both rig saves have him beaten.
GASCOIGNE_DEFEATED = 12411800
# The opening cutscene's event in Central Yharnam (m24_01): off, the next load plays it again.
OPENING_EVENT = 12410000
# Hidden by the host's story (event disable +0x28) and shown on the guest after its join until
# guests followed the host (Kaniel 2026-10-10: Iosefka at her clinic door, play-down-revive).
STORY_HIDDEN = "0x10014005"
IOSEFKA_AT = (-200.0, -40.5, 209.8)  # her census position on the rig
# 3 m from her on each side, facing her (a heading h faces (-sin h, cos h)).
IOSEFKA_VIEWS = [("south", -200.0, 206.8, 0.0), ("north", -200.0, 212.8, 3.1416),
                 ("west", -203.0, 209.8, -1.5708), ("east", -197.0, 209.8, 1.5708)]
# Up the hallway north of her: out of the other player's pictures.
IOSEFKA_WAIT = "-200.0 -40.5 218.0 0.0"
# The compass scenario's spots: the guest up that hallway, the host 5.2 m south of it.
COMPASS_GUEST = "-200.0 -40.5 218.0"
COMPASS_HOST = "-200.0 -40.5 212.8"
# 7 m from ENEMY (-216.7, 178.1), on the lamp's side: close enough for it to notice a player.
GUEST_ALONE_SPOT = "-210.0 -54.5 181.0"
GUEST_PAIR_SPOT = "-213.0 -54.5 184.0"  # 3-4 m from GUEST_ALONE_SPOT


# At her clinic's door, facing it (south), where Kaniel talked to her (2026-10-13): Cross goes
# through her talk (the first gives her blood vials, item lot 23000).
IOSEFKA_DOOR = "-200.0 -40.5 208.4 3.1416"
TALK_THROUGH = [step for _ in range(12) for step in ("press cross 200", "wait 2")]


# The Cathedral Ward lamp (Oedon Chapel): its talk character (0x10010075) stands at
# (16.15, -8.90, 103.47) turned to 30 degrees in m24_00's MSB ("warp chair, start point"), 1.5 m
# from where a trip there puts a player (17.58 -9.5 102.65, bloodborne_lamp_points.h). Neither
# player got a prompt from that side (rig 2026-10-14), so the use tries its front: 0.7 and 1 m
# straight ahead of it and 0.8 m at 45 degrees either side, facing it (a talk that opens is
# closed again).
WARD_LAMP_SPOTS = ("16.50 -9.5 104.08 -2.62", "16.65 -9.5 104.34 -2.62", "16.92 -9.5 103.68 -1.83",
                   "15.94 -9.5 104.24 2.88")


# The way from the Ward lamp through the gate to Central Yharnam (a guest's trail, playtest
# 2026-10-13: the maps meet at about 31 -9 95).
WARD_TO_YHARNAM = ("21.1 -9.5 100.6 23.1 -9.5 101.5 24.0 -9.5 99.7 25.6 -9.1 98.4 27.3 -9.1 96.9 "
                   "30.7 -8.9 94.9 32.6 -9.7 94.1 34.4 -10.6 92.7 36.0 -11.2 92.6")
YHARNAM_TO_WARD = ("34.4 -10.6 92.7 32.6 -9.7 94.1 30.7 -8.9 94.9 27.3 -9.1 96.9 25.6 -9.1 98.4 "
                   "24.0 -9.5 99.7 23.1 -9.5 101.5 21.1 -9.5 100.6 18.8 -9.5 102.3")


def USE_WARD_LAMP(role: str) -> list[str]:
    steps = []
    for index, spot in enumerate(WARD_LAMP_SPOTS):
        steps += [f"teleport {spot}", "wait 2", f"shot {role}-lamp-{index}",
                  "press cross 200", "wait 5", f"shot {role}-pressed-{index}", "press circle 200",
                  "wait 3", "press circle 200", "wait 3"]
    return steps


def IOSEFKA_LOOK(role: str) -> list[str]:
    """3 m south of Iosefka, a little to the side (the player's back hides what is straight
    ahead), a picture facing her."""
    return [f"teleport -199.2 {IOSEFKA_AT[1]} 206.8 0.0", "wait 3", f"shot {role}-iosefka"]
# Event flags neither rig character has: one of another map (the Forbidden Woods) and one of
# Central Yharnam's range. A guest's own world setting them must not reach the host's world (its
# save's progress, re-set after a reload, killed the clinic's beast in a new host's world).
# The dog 35 m from the first lamp (364 HP). Kaniel 2026-10-10: it died in a new host's world as
# the session formed, its loot on the ground: the guest's own world had posted it hurt and the
# host's was lowered to that before the join.
DOG = "0x10014009"
WARD_NPC = "0x10010075"  # the NPC sitting by the Oedon Chapel lamp (999 HP)
# Another ordinary enemy near the first lamp (suites.FRESH_ENEMY: real hits land on it).
SECOND_ENEMY = "0x1001402e"
CLINIC = "0x10014000"  # 573 HP, by Iosefka's door 32 m from the first lamp, 10 m up
# Gilbert (entity 2410781), 5.6 m from the second lamp: the map's story removes him (2004:29,
# the hide byte +0x28) seconds after every load, so he stood by the lamp until then (Kaniel's
# and Lisara's playtest 2026-10-15). The first lamp is 110 m away.
GILBERT = "0x1001400c"
GILBERT_SEEN = r"watched 0x1001400c drawn"
OTHER_MAP_FLAG = 12700131
THIS_MAP_FLAG = 12410999
FORBIDDEN_WOODS_LAMP_LIT = 12707810  # a lamp neither rig character has lit (see LampOfActiveFlag)
START = f"ensure-map {CENTRAL_YHARNAM} {CENTRAL_YHARNAM_LAMP}"
# An enemy by a player moved farther in one frame than any runs (a hand-over that jumped).
ENEMY_JUMPED = r"Bloodborne party: enemy [0-9a-f]+ jumped"
# Phase 5b: a world flag of Central Yharnam's block no event uses (the rig's saves have it off).
TEAM_WORLD_FLAG = 12410999
LEVEL_SYNC = 9200  # the SpEffect that scales a guest down to the host's level
# How a game joins: the port's team opens and joins the host's session itself (phase 6, no bell
# and no signs); shadPS4 rings the bells.
HOST_JOINS = (r"(?:auto-rejoin started search effect=9000|"
              r"Co-op team join: this host's session \d+ to the server)")
GUEST_JOINS = (r"(?:auto-rejoin started search effect=9005|"
               r"Bloodborne team join: joining the host's session .* joined=true)")
# The session must hold: a game that leaves the room (LeaveRoom) broke it.
SESSION_HOLDS = [r"sceNpMatching2LeaveRoom: called"]
# A downed player's kneel, on its own game or on another's copy of it (Kaniel 2026-10-15: none).
KNEEL = r"party death: (pose [1-9]\d* asked|.*'s copy kneels)"
# A member's game died: the others' games close the room it was in (and open a new one for its
# return), which check_party_log reports as leaving the session outside a trip.
CRASH_CLEANUP = [r"left the session outside a trip"]
# Party death (SHADPS4_BLOODBORNE_PARTY_DEATH): a lethal hit puts a member down while another
# stands on its map. A lethal fall is the hit the rig can deal: 1 HP left, then 25 m up
# (from the first lamp it lands on a ledge 15 m down, about 40% of max HP; 8 m up did nothing, and
# a 25 m drop kills outright).
PARTY_DEATH = {"SHADPS4_BLOODBORNE_PARTY_DEATH": "1"}
# After a real death each player has its own bloodstain where it fell (GameDataMan's record on
# this map) and no enemy takes it (no echo theft): the bloodstain state lines say so.
BLOODSTAIN_DROPPED = (r"bloodstain state map=0x18010000 has_mark=1 record_valid=\d+ "
                      r"record_30=-?\d+ record_38=\d+ record_block=0x18010000")
BLOODSTAIN_STOLEN = r"bloodstain state .* stain_hidden=1"
LETHAL_FALL = ["hurt self 1", "rise 25"]
# Enemy HP kept in step: over the HP board (shadPS4's path) or by the area's owner (the port,
# phase 4: one game runs each area's enemies, the others follow it).
HP_SHARED = r"enemy HP (?:shared over the HP board|from the area's owner)"
# A copy lowered to the others' HP: the board's damage (old path) or the owner's HP (phase 4).
HP_LOWERED = (r"(?:lowered \d+ enemies by the other members' damage|"
              r"this follower's copies took the owner's HP from the log)")
OWNER_RUNS = r"enemy owner: this game runs map 0x18010000's enemies"
OWNER_FOLLOWS = r"enemy owner: this game follows \w+ on map 0x18010000"
# A real death while a partner stands on the map, as a player has one (party death is always on in
# the port, Kaniel 2026-10-13): the lethal fall puts it down and it gives up (Circle held 3 s).
GIVE_UP = [*LETHAL_FALL, "wait-vital down 30", "wait 2", "press circle 3600", "wait-vital dead 15"]
# wipe3 spreads the three a few metres from the lamp before each falls, so nobody lands on another
# member's body (the UI thread, 2026-10-12: a body under the fall took its damage).
WIPE3_SPOT = {"host": "-199.74 -50.56 182.42", "guest": "-196.74 -50.56 179.42",
              "guest2": "-202.74 -50.56 179.42"}
# A first down takes three hits (R1 beside the downed member), at least 0.4 s apart. Each R1
# lunges the hitter on, out of reach (2.5 m) after one, so it steps back beside it before each.
# Revive hits until the downed member is up: how many depends on the striker's weapon (party death
# takes its attack rating in damage).
# The status page (its face is the player's portrait): Circle closes what the start shows (an item's
# text), Options opens the menu on Inventory, Right goes to Status, Cross opens it.
OPEN_STATUS = ["wait 5", "press circle 200", "wait 2", "press options 200", "wait 3",
               "press right 200", "wait 2", "press cross 200", "wait 4"]
CLOSE_MENU = ["press circle 200", "wait 2", "press circle 200", "wait 3"]
REVIVE_HITS = ["revive-downed 150"]  # real hits: an unarmed rig hunter needs about 24 landed punches


def only_kill(handle: str) -> list[str]:
    """No kill posted but this one: a false kill hides a living enemy from the others."""
    return [rf"kill board gets (?!1 kills, 0 back \(visit [^)]*\): {handle} )"]


def attack(target: str) -> list[str]:
    """A real kill of an ordinary enemy (the kill reward and the death run as in play; the
    game's own kill leaves one standing in a party)."""
    return [f"attack {target} 40"]


SCENARIOS = {
    "setup": {
        "about": "every rig game at the first Central Yharnam lamp, saved there (then "
                 "--save-baseline)",
        "roles": {
            "host": [START, "wait 20", "snapshot"],
            "guest": [START, "wait 20", "snapshot"],
        },
        "expect": {"host": [r"snapshot: map 0x18010000"], "guest": [r"snapshot: map 0x18010000"]},
        "timeout": 300,
        "no_baseline": True,
    },
    "boot": {
        "about": "a game gets from the title screen into the world by itself",
        "roles": {
            "host": [START, "snapshot", "wait 3", "snapshot"],
        },
        "expect": {"host": [r"Bloodborne autoplay snapshot: map 0x"]},
        "timeout": 300,
    },
    "join": {
        "about": "host and guest join each other by themselves (the port's team without a bell, "
                 "shadPS4's bells on their own); the guest keeps its full HP and its own level",
        "roles": {
            "host": [START, "wait-session 2 300", "wait 10", "snapshot", "expect-session 2"],
            "guest": [START, "note-max-hp", "wait-session 2 300", "wait 10", "snapshot",
                      "expect-session 2", "expect-max-hp self == noted",
                      f"expect-effect {LEVEL_SYNC} 0"],
        },
        "expect": {
            "host": [HOST_JOINS],
            "guest": [GUEST_JOINS],
        },
        "forbid": {"host": SESSION_HOLDS, "guest": SESSION_HOLDS},
        "timeout": 300,
    },
    "trip": {
        "about": "the host goes to the Hunter's Dream and back; the session holds and the players "
                 "are re-created by name",
        "roles": {
            "host": [START, "wait-session 2 300", "wait 10", f"travel {HUNTERS_DREAM}", "wait 5",
                     f"travel {CENTRAL_YHARNAM_LAMP}", "wait-members 2 120", "wait 15", "snapshot",
                     "expect-session 2"],
            "guest": [START, "wait-session 2 300", "wait 30", "wait-members 2 240", "wait 25",
                      "snapshot", "expect-session 2"],
        },
        "expect": {
            "host": [r"party trip started", r"re-creating rig_guest on map 0x18010000"],
        },
        "forbid": {"host": SESSION_HOLDS, "guest": SESSION_HOLDS},
        "timeout": 300,
    },
    "kill-apart": {
        "about": "the host kills an enemy while the guest is in the Dream; the guest's copy of it "
                 "is hidden when it comes back",
        "roles": {
            "host": [START, "godmode", "wait-session 2 300", "wait 45", *attack(ENEMY),
                     "wait 5", "wait-members 2 240", "wait 20", "snapshot"],
            "guest": [START, "godmode", "wait-session 2 300", "wait 10", f"travel {HUNTERS_DREAM}",
                      "wait 40", f"travel {CENTRAL_YHARNAM_LAMP}", "wait-members 2 240", "wait 25",
                      "snapshot"],
        },
        "expect": {
            "host": [r"kill board gets 1 kills"],
            "guest": [r"(?:hid 1 enemies a member killed on map 0x18010000|0x18010000, 1 near died)"],
        },
        "forbid": {"host": only_kill(ENEMY)},
        "timeout": 720,
    },
    "trade": {
        "about": "trading on the server (the port with the server world): "
                 "the host gives the guest 4 Blood Vials, swaps 3000 Blood Echoes for 5 Quicksilver "
                 "Bullets, then offers 2 vials the guest declines (they come back)",
        "roles": {
            "host": [START, "godmode", "wait-session 2 300", "wait 10",
                     "trade-stock vials 10", "trade-stock bullets 0", "trade-stock echoes 5000",
                     "wait 5",
                     "trade-offer vials 4 120", "expect-held vials 6", "wait-trade accepted 120",
                     "trade-offer echoes 3000 for bullets 5 60", "expect-held echoes 2000",
                     "expect-held bullets 5 60",
                     "trade-offer vials 2 60", "expect-held vials 4", "wait-trade declined 120",
                     "expect-held vials 6 30", "wait-trade returned 10"],
            "guest": [START, "godmode", "wait-session 2 300", "wait 10",
                      "trade-stock vials 0", "trade-stock bullets 10", "trade-stock echoes 0",
                      "trade-answer yes 180", "expect-held vials 4 30", "wait-trade received 10",
                      "trade-answer yes 120", "expect-held echoes 3000 30", "expect-held bullets 5",
                      "trade-answer no 120", "wait 10", "expect-held vials 4"],
        },
        "expect": {
            "host": [r"Bloodborne trade: offered .* 4 Blood Vials",
                     r"Bloodborne trade: added 5 Quicksilver Bullets",
                     r"Bloodborne trade: added 2 Blood Vials"],
            "guest": [r"Bloodborne trade: added 4 Blood Vials",
                      r"Bloodborne trade: added 3,000 Blood Echoes",
                      r"Bloodborne trade: declined offer"],
        },
        "forbid": {"host": SESSION_HOLDS, "guest": SESSION_HOLDS},
        "timeout": 300,
    },
    "trade-any": {
        "about": "trading any item (Kaniel 2026-10-15): the host gives the guest a key item (Oedon "
                 "Tomb Key), a weapon with a blood gem in it (Hunter Axe +5) and armor (Charred "
                 "Hunter Garb) in one offer; the guest gets each, the weapon with its gem",
        "roles": {
            "host": [START, "godmode", "wait-session 2 300", "wait 10",
                     "trade-stock goods:4000 1", "trade-stock weapon:5000500 1",
                     "trade-socket weapon:5000500 gem:1000000 13101",
                     "trade-stock armor:11000 1", "wait 5",
                     "trade-offer goods:4000 1 weapon:5000500 1 armor:11000 1 120",
                     "expect-held goods:4000 0", "expect-held weapon:5000500 0",
                     "expect-held armor:11000 0", "wait-trade accepted 120"],
            "guest": [START, "godmode", "wait-session 2 300", "wait 10",
                      "trade-stock goods:4000 0", "trade-stock weapon:5000500 0",
                      "trade-stock armor:11000 0",
                      "trade-answer yes 180", "expect-held goods:4000 1 30",
                      "expect-held weapon:5000500 1 30",
                      "expect-socketed weapon:5000500 gem:1000000 13101",
                      "expect-held armor:11000 1", "wait-trade received 10"],
        },
        "expect": {
            "host": [r"Bloodborne trade: offered .* Oedon Tomb Key, Hunter Axe \+5, Charred "
                     r"Hunter Garb"],
            "guest": [r"Bloodborne trade: added Oedon Tomb Key, Hunter Axe \+5, Charred Hunter "
                      r"Garb"],
        },
        "forbid": {"host": SESSION_HOLDS, "guest": SESSION_HOLDS},
        "timeout": 300,
    },
    "trade-menus": {
        "about": "trading's menus (drawn): the guest answers the "
                 "host's offer in the game's yes/no box and the host sees the game's box saying it "
                 "was accepted; then the host gives 2 vials through the inventory's Give row and the "
                 "game's quantity picker, and 2,000 Blood Echoes through Give Blood Echoes; the "
                 "guest accepts both",
        "roles": {
            "host": [START, "godmode", "wait-session 2 300", "wait 10",
                     "trade-stock vials 10", "trade-stock bullets 10", "trade-stock echoes 2000",
                     "wait 5", "trade-offer vials 4 120", "wait-trade accepted 120", "wait 3",
                     "shot host-notice", "press cross", "wait 2",
                     # Blood Vial (the first row) -> Give -> 2 in the picker.
                     "press options", "wait 2", "press cross", "wait 2", "shot host-inventory",
                     "press cross", "wait 2", "press down", "wait 1", "press down", "wait 1",
                     "shot host-commands-give", "press cross", "wait 2", "shot host-quantity",
                     "press up", "wait 1", "press left", "wait 1", "shot host-quantity-2",
                     "press cross", "wait 2",
                     "expect-held vials 4", "shot host-offered",
                     # Blood Vial -> Give Blood Echoes -> the picker's top (all of them).
                     "press cross", "wait 2", "press down", "wait 1", "press down", "wait 1",
                     "press down", "wait 1", "press cross", "wait 2", "shot host-echoes",
                     "press down", "wait 1", "press left", "wait 1", "shot host-echoes-max",
                     "press cross", "wait 2",
                     "expect-held echoes 0",
                     "press circle", "wait 1", "press circle", "wait 1", "press circle",
                     "wait 1", "press circle", "wait 40", "shot host-end"],
            "guest": [START, "godmode", "wait-session 2 300", "wait 10",
                      "trade-stock vials 0", "trade-stock echoes 0", "wait 40",
                      "shot guest-ask", "press left", "wait 1", "shot guest-ask-yes", "press cross",
                      "wait 3", "shot guest-received", "press cross", "expect-held vials 4 30",
                      "wait 30", "shot guest-ask-2", "press left", "wait 1", "press cross",
                      "wait 3", "shot guest-received-2", "expect-held vials 6 30",
                      "wait 10", "shot guest-ask-3", "press left", "wait 1", "press cross",
                      "wait 3", "shot guest-received-3", "expect-held echoes 2000 30"],
        },
        "expect": {
            "host": [r"Bloodborne trade: offered .* 4 Blood Vials",
                     r"Bloodborne trade: offered .* 2 Blood Vials",
                     r"Bloodborne trade: offered .* 2,000 Blood Echoes"],
            "guest": [r"Bloodborne trade: accepted offer", r"Bloodborne trade: added 4 Blood Vials",
                      r"Bloodborne trade: added 2,000 Blood Echoes"],
        },
        "forbid": {"host": SESSION_HOLDS, "guest": SESSION_HOLDS},
        "timeout": 900,
    },
    "trade-echoes": {
        "about": "giving Blood Echoes through the menus (Kaniel 2026-10-16: a clearer soul trade): "
                 "the host holding 54,321 opens Give Blood Echoes (the last row of any item's "
                 "commands); the picker opens on 1,000 and steps by 1,000, so Up twice is 3,000; "
                 "then Down from 1,000 wraps to all the rest; the guest accepts both",
        "roles": {
            "host": [START, "godmode", "wait-session 2 300", "wait 10",
                     "trade-stock vials 10", "trade-stock bullets 0", "trade-stock echoes 54321",
                     "wait 5", "press options", "wait 2", "press cross", "wait 2",
                     # The first row's commands -> Up wraps to Give Blood Echoes -> 3,000 -> Yes.
                     "press cross", "wait 2", "press up", "wait 1", "press cross", "wait 2",
                     "press up", "wait 1", "press up", "wait 1", "press left", "wait 1",
                     "press cross", "wait 3", "expect-held echoes 51321",
                     # The menu closes after an offer; the game's OK box says it was accepted.
                     "wait-trade accepted 60", "wait 2", "press cross", "wait 2",
                     # Again: Down from the first step is all of them.
                     "press options", "wait 2", "press cross", "wait 2",
                     "press cross", "wait 2", "press up", "wait 1", "press cross", "wait 2",
                     "press down", "wait 1", "press left", "wait 1", "press cross", "wait 3",
                     "expect-held echoes 0", "wait 5"],
            "guest": [START, "godmode", "wait-session 2 300", "wait 10",
                      "trade-stock echoes 0", "trade-answer yes 180",
                      "expect-held echoes 3000 30", "trade-answer yes 120",
                      "expect-held echoes 54321 30", "wait-trade received 10"],
        },
        "expect": {
            "host": [r"Bloodborne trade: offered .* 3,000 Blood Echoes",
                     r"Bloodborne trade: offered .* 51,321 Blood Echoes"],
            "guest": [r"Bloodborne trade: added 3,000 Blood Echoes",
                      r"Bloodborne trade: added 51,321 Blood Echoes"],
        },
        "forbid": {"host": SESSION_HOLDS, "guest": SESSION_HOLDS},
        "timeout": 300,
    },
    "server-restart": {
        "about": "phase 3, the party's world on the server (the port): the host kills an enemy while the guest is in the Dream, shadNet restarts "
                 "mid-session, the host kills another; the guest coming back finds both dead (a "
                 "server that lost the world would start a new one, and the first kill with it)",
        "roles": {
            "host": [START, "godmode", "wait-session 2 300", "wait 25", *attack(ENEMY),
                     "wait 5", "sync killed", "wait 20", *attack(SECOND_ENEMY), "wait 5",
                     "sync killed2", "wait-members 2 240", "wait 20", "snapshot"],
            "guest": [START, "godmode", "wait-session 2 300", "wait 10", f"travel {HUNTERS_DREAM}",
                      "sync killed", "sync killed2", "wait 5", f"travel {CENTRAL_YHARNAM_LAMP}",
                      "wait-members 2 240", "wait 25", "snapshot"],
        },
        "syncs": {"killed": {"chapter": "server-restart", "roles": ["host", "guest"]},
                  "killed2": {"chapter": "server-restart", "roles": ["host", "guest"]}},
        "server_restarts": ["killed"],
        "expect": {
            "host": [r"Co-op server world: applied .*kill 0x1001400b",
                     r"Co-op server world: shadNet started over",
                     rf"Co-op server world: applied .*kill {SECOND_ENEMY}"],
            "guest": [r"Co-op server world: shadNet started over",
                      rf"Co-op server world: applied .*kill {SECOND_ENEMY}",
                      r"(?:hid 2 enemies a member killed on map 0x18010000, 0 near died|"
                      r"hid 1 enemies a member killed on map 0x18010000, 1 near died|"
                      r"hid 0 enemies a member killed on map 0x18010000, 2 near died)"],
        },
        "forbid": {"guest": [r"Co-op server world: following the party's log 0x[0-9a-f]+0{7}3\b",
                             r"Co-op server world: shadNet refused"]},
        "timeout": 300,
    },
    "story-npc-trips": {
        "about": "Gilbert, whom the map's story removes seconds after each load, is not drawn "
                 "by the second Central Yharnam lamp after a trip there (Kaniel and Lisara "
                 "2026-10-15: he stood there a moment, then vanished): first the guest alone (the "
                 "host 110 m away at the first lamp), then the host with the guest there",
        "roles": {
            "host": [START, "godmode", "wait-session 2 300", "wait 30", "sync guest-back",
                     f"watch-unseen {GILBERT} 30", f"travel {HUNTERS_DREAM}", "wait 5",
                     f"travel {CENTRAL_YHARNAM_LAMP_2}", "wait 15", "snapshot",
                     "expect-session 2"],
            "guest": [START, "godmode", "wait-session 2 300", "wait 30",
                      f"watch-unseen {GILBERT} 30", f"travel {HUNTERS_DREAM}", "wait 5",
                      f"travel {CENTRAL_YHARNAM_LAMP_2}", "wait 15", "sync guest-back",
                      "wait 30", "snapshot", "expect-session 2"],
        },
        "syncs": {"guest-back": {"chapter": "story-npc-trips", "roles": ["host", "guest"]}},
        "forbid": {"host": [GILBERT_SEEN] + SESSION_HOLDS, "guest": [GILBERT_SEEN] + SESSION_HOLDS},
        "timeout": 300,
    },
    "dog": {
        "about": "Kaniel's dog: the guest kills an enemy while the host is in the Dream; the host "
                 "coming back finds it dead, not alive again",
        "roles": {
            "host": [START, "godmode", "wait-session 2 300", "wait 10", f"travel {HUNTERS_DREAM}",
                     "wait 40", f"travel {CENTRAL_YHARNAM_LAMP}", "wait-members 2 240", "wait 25",
                     "snapshot"],
            "guest": [START, "godmode", "wait-session 2 300", "wait 45", *attack(ENEMY),
                      "wait 5", "wait-members 2 240", "wait 20", "snapshot"],
        },
        "expect": {
            "guest": [r"kill board gets 1 kills"],
            "host": [r"(?:hid 1 enemies a member killed on map 0x18010000|0x18010000, 1 near died)"],
        },
        "forbid": {"guest": only_kill(ENEMY)},
        "timeout": 720,
    },
    "story-hidden": {
        "about": "a character the host's story hides (Iosefka) stays hidden on the guest after "
                 "its join: the guest hides and shows every character as the host does",
        "roles": {
            "host": [START, "godmode", "census 80", "wait-session 2 300", "wait 20", "census 80",
                     f"expect-hidden {STORY_HIDDEN} 1"],
            "guest": [START, "godmode", "census 80", "wait-session 2 300", "wait 20", "census 80",
                      f"expect-hidden {STORY_HIDDEN} 1 30", "wait 10",
                      f"expect-hidden {STORY_HIDDEN} 1"],
        },
        "expect": {"guest": [rf"characters as the host has them on map 0x18010000: hid \d+, "
                             rf"showed \d+:.* {STORY_HIDDEN}"]},
        "forbid": {"host": SESSION_HOLDS, "guest": SESSION_HOLDS},
        "timeout": 300,
    },
    "story-switched-on": {
        "about": "an enemy the guest's own story has switched off while the host's has it on "
                 "(Kaniel's friend 2026-10-15: the Forbidden Woods' enemy out of the red mist "
                 "never showed for her): the guest switches its copy on as the host has it",
        "roles": {
            "host": [START, "godmode", "wait-session 2 300", "wait 20",
                     f"expect-disabled {ENEMY} 0", "wait 30",
                     f"expect-disabled {ENEMY} 0"],
            "guest": [START, "godmode", "wait-session 2 300", "wait 20",
                      f"expect-disabled {ENEMY} 0", f"set-disabled {ENEMY} 1",
                      f"expect-disabled {ENEMY} 0 30", "wait 10", f"expect-disabled {ENEMY} 0"],
        },
        "expect": {"guest": [rf"autoplay set-disabled: {ENEMY} -> 1",
                             rf"characters switched like the host has them on map 0x18010000 "
                             rf"\(off, \+on\):.* \+{ENEMY}"]},
        "forbid": {"host": SESSION_HOLDS, "guest": SESSION_HOLDS},
        "timeout": 300,
    },
    "iosefka-new": {
        "about": "iosefka-look on a new character (Kaniel's, rig/saves/new-character): every "
                 "character nearby in both worlds, then Iosefka's dump and pictures (the guest "
                 "saw her on new characters, Kaniel 2026-10-10). The host's story removed her "
                 "before her model loaded; the guest switches its loaded copy off (2004:5), the "
                 "host's stays as its story left it",
        "saves": "new-character",
        "roles": {
            role: ["godmode", "census 300", "wait-session 2 300", "wait 20", "census 300",
                   f"expect-disabled {STORY_HIDDEN} {1 if role == 'guest' else 0} 30",
                   f"dump {STORY_HIDDEN}", f"dump {CLINIC}",
                   *[step for name, x, z, heading in IOSEFKA_VIEWS
                     for step in (f"teleport {x} {IOSEFKA_AT[1]} {z} {heading}", "wait 3",
                                  f"shot {role}-{name}")],
                   "census 300"]
            for role in ("host", "guest")
        },
        "forbid": {"host": SESSION_HOLDS, "guest": SESSION_HOLDS},
        "timeout": 300,
    },
    "iosefka-mine": {
        "about": "iosefka-new on the characters each game has now (no saves put back): Kaniel's "
                 "own new characters (2026-10-10, copies in rig/saves/kaniel-1010-<role>)",
        "no_baseline": True,
        "roles": None,  # iosefka-new's, set below
        "forbid": {"host": SESSION_HOLDS, "guest": SESSION_HOLDS},
        "timeout": 300,
    },
    "iosefka-gift": {
        "about": "each player gets its own gift from an NPC (Kaniel 2026-10-13: the host got "
                 "Iosefka's blood vials, the guest who talked to her too got nothing): the host "
                 "talks to Iosefka at her door, then the guest does, and each is given the vials",
        "roles": {
            "host": [START, "godmode", "wait-session 2 300", "wait 15",
                     f"teleport {IOSEFKA_DOOR}", "wait 3", "shot host-door", *TALK_THROUGH, "wait 5", "snapshot", f"teleport {IOSEFKA_WAIT}", "wait 90",
                     "expect-session 2"],
            "guest": [START, "godmode", "wait-session 2 300", f"teleport {IOSEFKA_WAIT}",
                      "wait 75", f"teleport {IOSEFKA_DOOR}", "wait 3", "shot guest-door",
                      "press cross 200", "wait 2", "shot guest-talk-1", *TALK_THROUGH, "wait 5",
                      "snapshot", "expect-session 2"],
        },
        "expect": {"host": [r"item lot 23000 awarded from"],
                   "guest": [r"gave the guest its NPC gift, item lot 23000 \(trigger 72410310\)"]},
        "forbid": {"host": SESSION_HOLDS, "guest": SESSION_HOLDS},
        "timeout": 600,
    },
    "iosefka-look": {
        "about": "Iosefka in the clinic's upstairs doorway (Kaniel's first screenshot: the guest "
                 "saw her there, the host did not): each player in turn looks at her from 3 m "
                 "south (the other waits up the hallway), then the guest tries the two flag "
                 "bits her character has in the host's world and not in its own (+0x1E1 0x80, "
                 "+0x1E4 1), a picture after each",
        "roles": {
            "host": [START, "godmode", "wait-session 2 300", "wait 20",
                     f"expect-hidden {STORY_HIDDEN} 1 30", f"dump {STORY_HIDDEN}",
                     *IOSEFKA_LOOK("host"), f"teleport {IOSEFKA_WAIT}", "wait 60"],
            "guest": [START, "godmode", "wait-session 2 300", "wait 20",
                      f"expect-hidden {STORY_HIDDEN} 1 30", f"dump {STORY_HIDDEN}",
                      f"teleport {IOSEFKA_WAIT}", "wait 20", *IOSEFKA_LOOK("guest"),
                      f"poke {STORY_HIDDEN} 1e4 1 1", "wait 2", "shot guest-poke-1e4",
                      f"poke {STORY_HIDDEN} 1e1 84 1", "wait 2", "shot guest-poke-1e1",
                      f"dump {STORY_HIDDEN}"],
        },
        "forbid": {"host": SESSION_HOLDS, "guest": SESSION_HOLDS},
        "timeout": 600,
    },
    "phantom-look": {
        "about": "how a guest is drawn (Kaniel 2026-10-11: no blue phantom tint on guests): "
                 "after the join each player dumps its own character and model and takes a "
                 "picture; the guest's own character shows the tint",
        "roles": {
            "host": [START, "godmode", "wait-session 2 300", "wait 20", "dump self",
                     "shot host-self", "wait 30"],
            "guest": [START, "godmode", "wait-session 2 300", "wait 20", "dump self",
                      "shot guest-self", "dump nearest", "wait 10"],
        },
        "forbid": {"host": SESSION_HOLDS, "guest": SESSION_HOLDS},
        "timeout": 300,
    },
    # Kaniel 2026-10-14: with the host far away on the same map, the enemies beside a guest stood
    # still and did not attack (the host's game runs the map's enemies, and woke only its own).
    "guest-alone-mobs": {
        "about": "the host far away on the same map (by Iosefka's door), the guest walks up to an "
                 "enemy alone: it comes for the guest (moves) and hits it",
        "roles": {
            "host": [START, "godmode", "wait-session 2 300", f"teleport {IOSEFKA_WAIT}",
                     "wait 75", "expect-session 2"],
            "guest": [START, "wait-session 2 300", "wait 15", f"census 40",
                      f"teleport {GUEST_ALONE_SPOT}", "wait 3", "census 40", "wait 25",
                      "census 40", "expect-session 2"],
        },
        "moves": {"guest": ENEMY},
        "forbid": {"host": SESSION_HOLDS, "guest": SESSION_HOLDS},
        "timeout": 600,
    },
    # Kaniel 2026-10-14: the guest's game runs an enemy by it while the host is far; its hits
    # still meet the host's HP (the owner applies them and sends the HP back).
    "guest-alone-hits": {
        "about": "the host far away on the same map, the guest hits the enemy it runs twice: "
                 "both games end at max HP less the guest's hits",
        "roles": {
            "host": [START, "godmode", "wait-session 2 300", f"teleport {IOSEFKA_WAIT}",
                     "wait 75", "census 60", "expect-session 2"],
            "guest": [START, "godmode", "wait-session 2 300", "wait 15",
                      f"teleport {GUEST_ALONE_SPOT}", "wait 8", f"hit {ENEMY} 30", "wait 1",
                      f"hit {ENEMY} 30", "wait 15", "census 40", "expect-session 2"],
        },
        "same_hp": ENEMY,
        "hp_sums_hits": ENEMY,
        "expect": {"guest": [r"hit on 0x1001400b by this player"]},
        "forbid": {"host": SESSION_HOLDS, "guest": SESSION_HOLDS},
        "timeout": 600,
    },
    # Kaniel 2026-10-14: never two games running one enemy. Two guests by it, the host far: the
    # nearer guest runs it (the logs say who runs which, and when).
    "guests-pair-mobs": {
        "about": "the host far away on the same map, both guests by one enemy: it comes for them, "
                 "run by one guest's game at a time",
        "roles": {
            "host": [START, "godmode", "wait-session 3 300", f"teleport {IOSEFKA_WAIT}",
                     "wait 80", "expect-session 3"],
            "guest": [START, "wait-session 3 300", "wait 15", f"teleport {GUEST_ALONE_SPOT}",
                      "wait 3", "census 40", "wait 25", "census 40", "expect-session 3"],
            "guest2": [START, "wait-session 3 300", "wait 15", f"teleport {GUEST_PAIR_SPOT}",
                       "wait 3", "census 40", "wait 25", "census 40", "expect-session 3"],
        },
        "moves": {"guest": ENEMY},
        "forbid": {"host": SESSION_HOLDS, "guest": SESSION_HOLDS, "guest2": SESSION_HOLDS},
        "timeout": 720,
    },
    # The host comes back to the enemy the guest runs: its game takes it back, and it goes on.
    "guest-mobs-host-comes": {
        "about": "the guest runs an enemy with the host far away, then the host comes beside it: "
                 "the host's game takes it back and it keeps fighting",
        "roles": {
            "host": [START, "godmode", "wait-session 2 300", f"teleport {IOSEFKA_WAIT}",
                     "wait 40", f"teleport {GUEST_ALONE_SPOT}", "wait 40", "expect-session 2"],
            "guest": [START, "godmode", "wait-session 2 300", "wait 15",
                      f"teleport {GUEST_ALONE_SPOT}", "wait 3", "census 40", "wait 25",
                      "census 40", "wait 30", "census 40", "expect-session 2"],
        },
        "moves": {"guest": ENEMY},
        "forbid": {"host": SESSION_HOLDS + [ENEMY_JUMPED],
                   "guest": SESSION_HOLDS + [ENEMY_JUMPED]},
        "timeout": 600,
    },
    # Kaniel 2026-10-15: the guest's hits on an enemy it ran came late, and the enemy went back to
    # full HP once the host came near. The guest's hits land at once; the host's copy takes them.
    "guest-hits-host-comes": {
        "about": "the guest hits the enemy it runs twice with the host far away, then the host "
                 "comes beside it: both games show the same HP, less the guest's hits",
        "roles": {
            "host": [START, "godmode", "wait-session 2 300", f"teleport {IOSEFKA_WAIT}",
                     "wait 50", f"teleport {GUEST_ALONE_SPOT}", "wait 25", "census 40",
                     "expect-session 2"],
            "guest": [START, "godmode", "wait-session 2 300", "wait 15",
                      f"teleport {GUEST_ALONE_SPOT}", "wait 8", f"hit {ENEMY} 30", "wait 1",
                      f"hit {ENEMY} 30", "wait 45", "census 40", "expect-session 2"],
        },
        "same_hp": ENEMY,
        "hp_sums_hits": ENEMY,
        "expect": {"guest": [r"hit on 0x1001400b by this player"]},
        "forbid": {"host": SESSION_HOLDS, "guest": SESSION_HOLDS},
        "timeout": 600,
    },
    # Kaniel 2026-10-15: both on Central Yharnam, the guest a bit away from the host: every enemy
    # lagged (about 200 ms) and acted dumb, run by the host. As in vanilla, the host's game hands
    # the enemies only the guest is near to the guest's game, which runs them and sends their
    # positions (the host's copy takes them often, for a smooth hand-over back).
    "guest-far-positions": {
        "about": "the host far away on the same map, the enemy by the guest comes for it: the "
                 "host's game hands it to the guest's, which runs it; no enemy jumps",
        "roles": {
            "host": [START, "godmode", "wait-session 2 300", f"teleport {IOSEFKA_WAIT}",
                     "wait 75", "expect-session 2"],
            "guest": [START, "godmode", "wait-session 2 300", "wait 15",
                      f"teleport {GUEST_ALONE_SPOT}", "wait 3", "census 40", "wait 40",
                      "census 40", "expect-session 2"],
        },
        "moves": {"guest": ENEMY},
        "expect": {"host": [r"enemy positions go out every 0\.10 s",
                            r"the game handed [1-9]\d* enemies to the others"],
                   "guest": [r"enemy follower runs [1-9]\d* enemies the owner handed it"]},
        "forbid": {"host": SESSION_HOLDS + [ENEMY_JUMPED], "guest": SESSION_HOLDS + [ENEMY_JUMPED]},
        "timeout": 600,
    },
    # Its control: the host beside the guest, the same enemy comes for them.
    "guest-mobs-host-near": {
        "about": "guest-alone-mobs with the host beside the guest: the enemy comes for them",
        "roles": {
            "host": [START, "godmode", "wait-session 2 300", "wait 18",
                     f"teleport {GUEST_ALONE_SPOT}", "wait 40", "expect-session 2"],
            "guest": [START, "wait-session 2 300", "wait 15", f"census 40",
                      f"teleport {GUEST_ALONE_SPOT}", "wait 3", "census 40", "wait 25",
                      "census 40", "expect-session 2"],
        },
        "moves": {"guest": ENEMY},
        "forbid": {"host": SESSION_HOLDS, "guest": SESSION_HOLDS},
        "timeout": 600,
    },
    "hp-together": {
        "about": "both players hit one enemy in one world (after the join), then the host goes "
                 "to the Dream and back, then the guest, and both hit it again from their own "
                 "worlds; each time both games show the same HP with both players' damage in it, "
                 "and at the end each sees the other (Kaniel 2026-10-10: HP bars apart after "
                 "trips; host and guest unseen after the host's early trip). The guest hits once "
                 "the second time: its weapon kills the enemy in two more",
        "roles": {
            "host": [START, "godmode", "wait-session 2 300", "wait 30", "census 40",
                     f"hit {ENEMY} 30", "wait 1", f"hit {ENEMY} 30", "wait 12", "census 40",
                     "wait 10", f"travel {HUNTERS_DREAM}", "wait 5", f"travel {CENTRAL_YHARNAM_LAMP}",
                     "wait-members 2 240", "wait 70", "census 40", f"hit {ENEMY} 30", "wait 1",
                     f"hit {ENEMY} 30", "wait 8", "census 40", "wait 4", "census 40",
                     "expect-others-near 1 40", "expect-session 2"],
            "guest": [START, "godmode", "wait-session 2 300", "wait 30", "census 40",
                      f"hit {ENEMY} 30", "wait 1", f"hit {ENEMY} 30", "wait 12", "census 40",
                      "wait 25", "wait-members 2 240", "wait 5", f"travel {HUNTERS_DREAM}",
                      "wait 5", f"travel {CENTRAL_YHARNAM_LAMP}", "wait-members 2 240", "wait 15",
                      "census 40", f"hit {ENEMY} 30", "wait 46",
                      "census 40", "wait 4", "census 40", "expect-others-near 1 40",
                      "expect-session 2"],
        },
        # The second census of each (after the hits in one world), and the last.
        "same_hp": ENEMY,
        "same_hp_census": [1],
        "expect": {"host": [HP_SHARED],
                   "guest": [HP_SHARED]},
        "forbid": {"host": SESSION_HOLDS, "guest": SESSION_HOLDS},
        "timeout": 900,
    },
    "early-trip": {
        "about": "the host goes to the Dream and back while the guest is still loading in: back "
                 "on the map the host asks the guest for its character data and both see each "
                 "other; then the guest raises a stat (a level up) and the host gets the new data "
                 "(Kaniel 2026-10-10: guest unseen after the host's early trip; char info "
                 "updated for everybody on a level up)",
        "roles": {
            "host": [START, "godmode", "wait-session 2 300", "wait 2", f"travel {HUNTERS_DREAM}",
                     "wait 5", f"travel {CENTRAL_YHARNAM_LAMP}", "wait-members 2 240", "wait 40",
                     "census 40", "expect-others-near 1 40", "wait 30", "expect-session 2"],
            "guest": [START, "godmode", "wait-session 2 300", "wait 60", "wait-members 2 240",
                      "wait 20", "census 40", "expect-others-near 1 40", "raise-stat 0 1",
                      "wait 15", "expect-session 2"],
        },
        "expect": {"host": [r"asked a member still joining for its data",
                            r"a member that was still joining sent its data",
                            r"member slot \d stats or face changed"],
                   "guest": [r"stats or face changed; sent its data"]},
        "forbid": {"host": SESSION_HOLDS, "guest": SESSION_HOLDS},
        "timeout": 800,
    },
    "ward-npc": {
        "about": "both players by the Cathedral Ward lamp (Oedon Chapel): the host hurts the NPC "
                 "there and the guest's copy follows (the game sends no NPC HP), then the guest "
                 "hits it and the host's copy follows; one HP in both games (Kaniel 2026-10-10)",
        "roles": {
            "host": [START, f"ensure-map {CATHEDRAL_WARD} {CATHEDRAL_WARD_LAMP}", "godmode",
                     "wait-session 2 420", "wait 15", f"hurt {WARD_NPC} 600", "wait 40", "census 60", "wait 10", "census 60",
                     "expect-session 2"],
            "guest": [START, f"ensure-map {CATHEDRAL_WARD} {CATHEDRAL_WARD_LAMP}", "godmode",
                      "wait-session 2 420", "wait 25", "census 60",
                      f"expect-hp {WARD_NPC} <= 600", "peek self 88", f"peek {WARD_NPC} 88",
                      f"hit {WARD_NPC} 20", "wait 8",
                      "census 60", "wait 20", "census 60", "expect-session 2"],
        },
        "same_hp": WARD_NPC,
        "forbid": {"host": SESSION_HOLDS, "guest": SESSION_HOLDS},
        "timeout": 700,
    },
    "hurt-before-join": {
        "about": "an enemy hurt in the guest's own world before it joins stays whole in the "
                 "host's: no HP crosses worlds outside a session, and the session forming "
                 "kills nothing (the dog by the first lamp)",
        "roles": {
            "host": [START, "godmode", "wait 25", f"expect-hp {DOG} == 364",
                     "wait-session 2 300", "wait 30", "census 60", f"expect-hp {DOG} == 364",
                     "expect-session 2"],
            "guest": [START, "godmode", f"hurt {DOG} 5", "wait-session 2 300", "wait 30",
                      "census 60", f"expect-hp {DOG} > 300", "expect-session 2"],
        },
        "forbid": {"host": SESSION_HOLDS + [r"Bloodborne echoes: \+\d+"],
                   "guest": SESSION_HOLDS},
        "timeout": 300,
    },
    "clinic": {
        "about": "both players by Iosefka's Clinic in the session: what each world shows and "
                 "kills there (Kaniel 2026-10-10: the clinic's beast dead in a new host's world, "
                 "with its loot)",
        "roles": {
            "host": [START, "godmode", "wait-session 2 300", "wait 10", f"approach {CLINIC} 2 0",
                     "wait 5", "census 40", "wait 40", "census 40", "expect-session 2"],
            "guest": [START, "godmode", "wait-session 2 300", "wait 15",
                      f"approach {CLINIC} -2 0", "wait 5", "census 40", "wait 40", "census 40",
                      "expect-session 2"],
        },
        "forbid": {"host": SESSION_HOLDS, "guest": SESSION_HOLDS},
        "timeout": 600,
    },
    "guest-flags-stay": {
        "about": "flags a guest's own world sets in the session stay in it: the host's world "
                 "does not get them (lamps and boss fogs go over the party's flag board)",
        "roles": {
            "host": [START, "godmode", f"expect-flag {OTHER_MAP_FLAG} 0",
                     f"expect-flag {THIS_MAP_FLAG} 0", "wait-session 2 300", "wait 45",
                     f"expect-flag {OTHER_MAP_FLAG} 0", f"expect-flag {THIS_MAP_FLAG} 0",
                     "expect-session 2"],
            "guest": [START, "godmode", "wait-session 2 300", "wait 10",
                      f"set-flag {OTHER_MAP_FLAG} 1", f"set-flag {THIS_MAP_FLAG} 1", "wait 40",
                      f"expect-flag {OTHER_MAP_FLAG} 1", "expect-session 2"],
        },
        "forbid": {"host": SESSION_HOLDS, "guest": SESSION_HOLDS},
        "timeout": 600,
    },
    "hp-apart": {
        "about": "after the guest's trip each runs its own copy of the map; the host hurts an "
                 "enemy and the guest's copy follows",
        "roles": {
            "host": [START, "godmode", "wait-session 2 300", "wait 55", "wait-members 2 240",
                     "wait 20", f"hurt {ENEMY} 200", "wait 12", "snapshot",
                     f"expect-hp {ENEMY} <= 200"],
            "guest": [START, "godmode", "wait-session 2 300", "wait 10", f"travel {HUNTERS_DREAM}",
                      "wait 10", f"travel {CENTRAL_YHARNAM_LAMP}", "wait-members 2 240", "wait 45",
                      "snapshot", f"expect-hp {ENEMY} <= 200"],
        },
        "expect": {"guest": [HP_LOWERED]},
        "timeout": 720,
    },
    "guest-drops": {
        "about": "Kaniel 2026-10-16: the enemies the host kills drop in the guest's world with a "
                 "handle each (krolebord found every kill's drop on the first wolf: one pickup "
                 "took them all)",
        "roles": {
            "host": [START, "godmode", "wait-session 2 300", "wait 15", f"kill {ENEMY}",
                     "wait 5", f"kill {DOG}", "wait 5", f"kill {BY_THE_LAMP}", "wait 30",
                     "snapshot", "expect-session 2"],
            "guest": [START, "godmode", "wait-session 2 300", "wait 70", "snapshot",
                      "expect-session 2"],
        },
        "forbid": {"host": SESSION_HOLDS,
                   "guest": SESSION_HOLDS + [r"0x38: [1-9]\d* items, \d+ usable, 0 with handle"]},
        "timeout": 600,
    },
    "boss-kill": {
        "about": "the Cleric Beast dies in the host's world with the guest there; the session "
                 "holds (no leave after the boss-clear script)",
        "roles": {
            "host": [START, "godmode", "wait-session 2 300", "wait 15", f"kill {CLERIC_BEAST}",
                     f"wait-flag {CLERIC_BEAST_DEFEATED} 1 60", "wait 40", "snapshot",
                     "expect-session 2"],
            "guest": [START, "godmode", "wait-session 2 300", "wait 90", "snapshot",
                      "expect-session 2"],
        },
        "expect": {"host": [r"flags in session: set [^|]*\b4000\b"]},
        "forbid": {"host": SESSION_HOLDS, "guest": SESSION_HOLDS},
        "timeout": 600,
    },
    "boss-refight": {
        "about": "the host fights Father Gascoigne, whom the guest beat long ago in its own "
                 "world: he stays in the host's world (the guest's old kill must not hide him) "
                 "and dies there with the guest in the session",
        "roles": {
            "host": [START, f"set-flag {GASCOIGNE_DEFEATED} 0", f"travel {CENTRAL_YHARNAM_LAMP}",
                     "godmode", "wait-session 2 300", "wait 20", f"expect-hp {GASCOIGNE} > 0",
                     f"kill {GASCOIGNE}", f"wait-flag {GASCOIGNE_DEFEATED} 1 60", "wait 20",
                     "snapshot", "expect-session 2"],
            "guest": [START, "godmode", "wait-session 2 300", "wait 90", "snapshot",
                      "expect-session 2"],
        },
        # Before the fight nothing hides Gascoigne; once the host kills him the guest sees him die
        # in the session and posts that, which is fine.
        "forbid": {
            "host": SESSION_HOLDS + [r"(?s)hid \d+ enemies a member killed.*autoplay: kill "
                                     r"0x1001409c"],
            "guest": SESSION_HOLDS,
        },
        "timeout": 720,
    },
    "gascoigne-together": {
        "about": "neither player has beaten Father Gascoigne; the guest hurts him in the session and "
                 "the host kills his beast form: afterwards both worlds have him down and no fight "
                 "left (playtest 2026-10-13: a guest's world kept his first form alive at 1 HP, and "
                 "the lamp by his arena answered with his talk)",
        "roles": {
            "host": [START, f"set-flag {GASCOIGNE_DEFEATED} 0", f"travel {CENTRAL_YHARNAM_LAMP}",
                     "godmode", "wait-session 2 300", "wait 40", f"dump {GASCOIGNE}",
                     f"dump {GASCOIGNE_BEAST}", f"kill {GASCOIGNE_BEAST}",
                     f"wait-flag {GASCOIGNE_DEFEATED} 1 60", "wait 30", f"dump {GASCOIGNE}",
                     f"dump {GASCOIGNE_BEAST}", "snapshot", "expect-session 2"],
            "guest": [START, f"set-flag {GASCOIGNE_DEFEATED} 0", f"travel {CENTRAL_YHARNAM_LAMP}",
                      "godmode", "wait-session 2 300", "wait 20", f"hurt {GASCOIGNE} 3000",
                      f"hurt {GASCOIGNE_BEAST} 3000", f"wait-flag {GASCOIGNE_DEFEATED} 1 120",
                      "wait 30", f"dump {GASCOIGNE}", f"dump {GASCOIGNE_BEAST}", "census 80",
                      "snapshot", "expect-session 2"],
        },
        "forbid": {"host": SESSION_HOLDS, "guest": SESSION_HOLDS},
        "timeout": 900,
    },
    "boss-guest-runs": {
        "about": "Kaniel 2026-10-16: a guest alone at a boss-sized enemy (max HP 1500 or more; "
                 "the big one by the first lamp) while the host stays back: the guest's game runs "
                 "it (no copy following the host's positions), in both games' eyes",
        "roles": {
            "host": [START, "godmode", "wait-session 2 300", "wait 90", "snapshot",
                     "expect-session 2"],
            "guest": [START, "godmode", "wait-session 2 300", "wait 20",
                      f"approach {BIG_BY_THE_LAMP} 4 0", "wait 10", f"hurt {BIG_BY_THE_LAMP} 300",
                      "wait 50", "snapshot", "expect-session 2"],
        },
        "expect": {"guest": [r"boss 0x10014001 is run by 7269675f6775657374"]},
        "forbid": {"host": SESSION_HOLDS,
                   "guest": SESSION_HOLDS + [r"enemy 10014001 jumped"]},
        "timeout": 600,
    },
    "save-backups": {
        "about": "the saves are backed up by themselves (Kaniel 2026-10-11): with a backup every "
                 "minute, the first one lands a minute after the start, in user/save-backups",
        "roles": {"host": [START, "wait 150", "snapshot"]},
        "settings": {"SHADPS4_BLOODBORNE_SAVE_BACKUPS": "1,2"},
        "expect": {"host": [r"Bloodborne save backups: every 1 min", r"Bloodborne save backup: "]},
        "timeout": 420,
    },
    "boss-putdown": {
        "about": "the reverse of boss-refight (Kaniel 2026-10-11, Prey Slaughtered after a "
                 "teleport): the host beat Father Gascoigne long ago, the guest has not; after "
                 "the host's trip its world puts him down again, and the guest's own boss event "
                 "must not take that for its kill (no banner, echoes, trophy or defeated flag)",
        "roles": {
            "host": [START, "godmode", "wait-session 2 300", "wait 20", f"travel {HUNTERS_DREAM}",
                     "wait 5", f"travel {CENTRAL_YHARNAM_LAMP}", "wait-members 2 240", "wait 60",
                     f"expect-flag {GASCOIGNE_DEFEATED} 1", "expect-session 2"],
            "guest": [START, f"set-flag {GASCOIGNE_DEFEATED} 0", f"travel {CENTRAL_YHARNAM_LAMP}",
                      "godmode", f"expect-hp {GASCOIGNE} > 0", "wait-session 2 300", "wait 30",
                      "wait-members 2 240", "wait 60", f"dump {GASCOIGNE}", "expect-session 2"],
        },
        "forbid": {
            "host": SESSION_HOLDS,
            "guest": SESSION_HOLDS + [r"sharing trophy 14 ", r"Bloodborne echoes: \+3200 "],
        },
        "timeout": 900,
    },
    "prey-replay": {
        "about": "Kaniel 2026-10-11 (Prey Slaughtered), on the characters each game has now (no saves put back; copy the "
                 "ones to test into rig/port/<role> first): the host waits in the Dream while the "
                 "guest joins in place and runs its map alone, then comes back, and Father Gascoigne must stay alive",
        "no_baseline": True,
        "roles": {
            "host": [START, "godmode", f"travel {HUNTERS_DREAM}", "wait-session 2 300", "wait 60",
                     f"travel {CENTRAL_YHARNAM_LAMP}", "wait-members 2 240", "wait 60",
                     f"expect-hp {GASCOIGNE} > 0", "expect-session 2"],
            "guest": [START, "godmode", f"expect-flag {GASCOIGNE_DEFEATED} 0",
                      "wait-session 2 300", "wait 60", "wait-members 2 240", "wait 60",
                      f"expect-hp {GASCOIGNE} > 0", "expect-session 2"],
        },
        "forbid": {
            "host": SESSION_HOLDS + [r"sharing trophy 14 "],
            "guest": SESSION_HOLDS + [r"sharing trophy 14 ", r"Bloodborne echoes: \+3200 "],
        },
        "timeout": 900,
    },
    "lamp-share": {
        "about": "a lamp the guest lights is lit in the host's world too",
        "roles": {
            "host": [START, f"expect-flag {FORBIDDEN_WOODS_LAMP_LIT} 0", "wait-session 2 300",
                     f"wait-flag {FORBIDDEN_WOODS_LAMP_LIT} 1 90", "snapshot"],
            # The guest's world loads again 10-20 s after the join; a flag set in the same 3 s
            # reads as part of that load and is not shared, so the lamp is lit after it.
            "guest": [START, "wait-session 2 300", "wait 30",
                      f"set-flag {FORBIDDEN_WOODS_LAMP_LIT} 1", "wait 30", "snapshot"],
        },
        "timeout": 300,
    },
    "join-ward": {
        "about": "joining in Cathedral Ward, whose co-op the guest's own world has not opened",
        "roles": {
            "host": [START, f"ensure-map {CATHEDRAL_WARD} {CATHEDRAL_WARD_LAMP}", "godmode",
                     "wait-session 2 360", "wait 10", "snapshot", "expect-session 2"],
            "guest": [START, f"ensure-map {CATHEDRAL_WARD} {CATHEDRAL_WARD_LAMP}", "godmode",
                      "wait-session 2 360", "wait 10", "snapshot", "expect-session 2"],
        },
        "expect": {"host": [r"join-anywhere patch"]},
        "forbid": {"host": SESSION_HOLDS, "guest": SESSION_HOLDS},
        "timeout": 300,
    },
    "join-apart": {
        "about": "joining across maps: the host in Cathedral Ward, the guest in Central Yharnam; "
                 "the guest joins where it is and the session holds (no kick)",
        "roles": {
            "host": [START, f"ensure-map {CATHEDRAL_WARD} {CATHEDRAL_WARD_LAMP}", "godmode",
                     "wait-session 2 360", "wait 60", "snapshot", "expect-session 2",
                     f"expect-map {CATHEDRAL_WARD}"],
            "guest": [START, "godmode", "wait-session 2 360", "wait 60", "snapshot",
                      "expect-session 2", f"expect-map {CENTRAL_YHARNAM}"],
        },
        "forbid": {"host": SESSION_HOLDS, "guest": SESSION_HOLDS},
        "timeout": 720,
    },
    "join-after-trip": {
        "about": "a guest that travelled on its own before any session (in its own world) still "
                 "rings and joins the host on another map",
        "roles": {
            "host": [START, "godmode", "wait-session 2 420", "wait 30", "snapshot",
                     "expect-session 2", f"expect-map {CENTRAL_YHARNAM}"],
            "guest": [START, f"ensure-map {CATHEDRAL_WARD} {CATHEDRAL_WARD_LAMP}", "godmode",
                      "wait-session 2 420", "wait 30", "snapshot", "expect-session 2",
                      f"expect-map {CATHEDRAL_WARD}"],
        },
        "forbid": {"host": SESSION_HOLDS, "guest": SESSION_HOLDS},
        "timeout": 300,
    },
    "lone-guest": {
        "about": "a guest alone on a map runs its enemies, and hands them back when the host comes",
        "roles": {
            "host": [START, "godmode", "wait-session 2 300", "wait 60",
                     f"travel {CATHEDRAL_WARD_LAMP}", "wait-members 2 240", "wait 20",
                     "snapshot"],
            "guest": [START, "godmode", "wait-session 2 300", "wait 10",
                      f"travel {CATHEDRAL_WARD_LAMP}", "wait-members 2 300", "wait 25",
                      "snapshot"],
        },
        "expect": {
            "guest": [r"(?:guest alone on map 0x18000000 took over \d+ enemies|"
                      r"enemy owner: took over \d+ enemies on map 0x18000000)",
                      r"(?:guest no longer alone; handed back|"
                      r"enemy owner: this game follows \w+ on map 0x18000000)"],
        },
        "timeout": 720,
    },
    "echoes-apart": {
        "about": "the guest kills an enemy alone in Cathedral Ward; the host in Central Yharnam "
                 "gets its echoes",
        "roles": {
            "host": [START, "godmode", "wait-session 2 300", "wait 80", "snapshot"],
            "guest": [START, "godmode", "wait-session 2 300", "wait 10",
                      f"travel {CATHEDRAL_WARD_LAMP}", "wait 10", "snapshot",
                      *attack(WARD_ENEMY), "wait 15", "snapshot"],
        },
        "expect": {
            "guest": [r"Bloodborne echoes: \+\d+ \(rates 1\) from 0x18fc99b"],
            "host": [r"added \d+ echoes from a kill another member made apart"],
        },
        "timeout": 600,
    },
    "host-death": {
        "about": "the host dies (goes down and gives up); it gets up without a reload and the "
                 "session holds",
        "settings": PARTY_DEATH,
        "roles": {
            "host": [START, "wait-session 2 300", "wait 15", *GIVE_UP, "wait 40", "snapshot",
                     "expect-session 2"],
            "guest": [START, "godmode", "wait-session 2 300", "wait-member-vital down 120",
                      "wait 60", "snapshot", "expect-session 2"],
        },
        "expect": {"host": [r"party death: giving up ends down \d+; dies \(reset=false",
                            r"live player died in a party session"]},
        "forbid": {"host": SESSION_HOLDS, "guest": SESSION_HOLDS},
        "timeout": 600,
    },
    "guest-death": {
        "about": "the guest dies (goes down and gives up) in the host's world; it gets up there "
                 "and the session holds",
        "settings": PARTY_DEATH,
        "roles": {
            "host": [START, "godmode", "wait-session 2 300", "wait-member-vital down 120",
                     "wait 60", "snapshot", "expect-session 2"],
            "guest": [START, "wait-session 2 300", "wait 15", *GIVE_UP, "wait 40", "snapshot",
                      "expect-session 2"],
        },
        "expect": {"guest": [r"party death: giving up ends down \d+; dies \(reset=false",
                             r"party guest died; revived"]},
        "forbid": {"host": SESSION_HOLDS, "guest": SESSION_HOLDS},
        "timeout": 600,
    },
    "host-crash": {
        "about": "the host's game crashes (the GPU's device lost does that in play); the guest "
                 "stays where it is, and they join again once the host is back",
        "roles": {
            "host": [[START, "godmode", "wait-session 2 300", "wait 15", "done"],
                     [START, "godmode", "wait-session 2 420", "wait 10", "snapshot",
                      "expect-session 2"]],
            "guest": [START, "godmode", "wait-session 2 300", "wait-solo 120", "wait 10",
                      "snapshot", f"expect-map {CENTRAL_YHARNAM}", "wait-session 2 420", "wait 10",
                      "snapshot", "expect-session 2"],
        },
        "allow": CRASH_CLEANUP,
        "timeout": 300,
    },
    "guest-crash": {
        "about": "the guest's game crashes; the host plays on, and they join again once the guest "
                 "is back (a dead game's sign must not stall the host)",
        "roles": {
            "host": [START, "godmode", "wait-session 2 300", "wait-solo 120", "wait 10",
                     "snapshot", f"expect-map {CENTRAL_YHARNAM}", "wait-session 2 420", "wait 10",
                     "snapshot", "expect-session 2"],
            "guest": [[START, "godmode", "wait-session 2 300", "wait 15", "done"],
                      [START, "godmode", "wait-session 2 420", "wait 10", "snapshot",
                       "expect-session 2"]],
        },
        "allow": CRASH_CLEANUP,
        "timeout": 300,
    },
    "resume": {
        "about": "a guest whose game dies in the host's world comes back where it was (its own "
                 "save still has it at the lamp it joined from) and joins the host again",
        "roles": {
            "host": [START, "godmode", "wait-session 2 300", "wait 40", "wait-solo 120",
                     "wait-session 2 420", "wait 20", "snapshot", "expect-session 2"],
            "guest": [[START, "godmode", "wait-session 2 300", "wait 10",
                       f"teleport {AWAY_FROM_LAMP}", "wait 15", "snapshot", "done"],
                      ["godmode", "wait-session 2 420", "wait 10", "snapshot",
                       "expect-session 2", f"expect-map {CENTRAL_YHARNAM}"]],
        },
        "expect": {"guest": [r"guest resumed at \(-21\d(\.\d+)?, -5\d(\.\d+)?, "
                             r"17\d(\.\d+)?\) on map 0x18010000"]},
        "allow": CRASH_CLEANUP,
        "timeout": 900,
    },
    "crash-apart": {
        "about": "a guest's game dies while it is apart from the host (in the Ward); the host "
                 "stays in its world (it once went to the title screen) and they join again",
        "roles": {
            "host": [START, "godmode", "wait-session 2 300", "wait 40", "wait-solo 120",
                     "wait 30", "snapshot", f"expect-map {CENTRAL_YHARNAM}",
                     "wait-session 2 420", "wait 20", "snapshot", "expect-session 2"],
            "guest": [[START, "godmode", "wait-session 2 300", "wait 10",
                       f"travel {CATHEDRAL_WARD_LAMP}", "wait 25", "snapshot", "done"],
                      ["godmode", f"wait-map {CATHEDRAL_WARD} 180", "wait-session 2 420",
                       "wait 10", "snapshot", "expect-session 2", f"expect-map {CATHEDRAL_WARD}"]],
        },
        "allow": CRASH_CLEANUP,
        "timeout": 900,
    },
    "host-crash-apart": {
        "about": "the host's game dies while the guest is apart from it (in the Ward); the guest "
                 "stays there, rings from there, and they join again without meeting first",
        "roles": {
            "host": [[START, "godmode", "wait-session 2 300", "wait 60", "done"],
                     [START, "godmode", "wait-session 2 420", "wait 20", "snapshot",
                      "expect-session 2", f"expect-map {CENTRAL_YHARNAM}"]],
            "guest": [START, "godmode", "wait-session 2 300", "wait 10",
                      f"travel {CATHEDRAL_WARD_LAMP}", "wait 15", "snapshot",
                      f"expect-map {CATHEDRAL_WARD}", "wait-solo 180", "wait 10", "snapshot",
                      f"expect-map {CATHEDRAL_WARD}", "wait-session 2 420", "wait 20",
                      "snapshot", "expect-session 2", f"expect-map {CATHEDRAL_WARD}"],
        },
        "allow": CRASH_CLEANUP,
        "timeout": 1080,
    },
    # Phase 5: the team lobby. The party is no setting: on the title the host creates a team and
    # invites, the guests accept; each one's Continue is its Ready, the host's is the team's Start.
    "team-join": {
        "about": "a team from the lobby: the host creates it and invites the guest, both press "
                 "Continue and play in one session (no party setting)",
        "roles": {
            "host": ["team-create", "team-invite rig_guest", "team-wait 2", "team-start", START,
                     "wait-session 2 300", "wait 10", "snapshot", "expect-session 2"],
            "guest": ["team-accept", "team-ready", START, "wait-session 2 300", "wait 10",
                      "snapshot", "expect-session 2"],
        },
        "expect": {
            "host": [r"Co-op team lobby: the save plays in team \w+ as its host",
                     r"team lobby: a save's load waits for the team's Start"],
            "guest": [r"Co-op team lobby: the save plays in team \w+ as its guest"],
        },
        "forbid": {"host": SESSION_HOLDS, "guest": SESSION_HOLDS},
        "timeout": 300,
    },
    # Kaniel 2026-10-15: a cutscene under the team's loading screen skips by itself (Options
    # pressed for the game's own skip). The opening's event (12410000, m24_01's emevd) plays its
    # cutscene again at the next load once its flag is off: the host clears it, and its next team
    # load plays the opening under the screen.
    "cutscene-skip": {
        "about": "the opening cutscene plays under the team's loading screen and skips by itself "
                 "(Options pressed); the screen opens and both are in one session",
        "roles": {
            "host": [["team-create", "team-invite rig_guest", "team-wait 2", "team-start", START,
                      "wait-session 2 300", f"set-flag {OPENING_EVENT} 0", "wait 15", "done"],
                     # The guest back at the title and ready first: both load together.
                     ["title-wait 60", "team-start", START, "wait-session 2 300", "wait 30",
                      "expect-session 2"]],
            "guest": [["team-accept", "team-ready", START, "wait-session 2 300", "wait 30",
                       "done"],
                      ["team-ready", START, "wait-session 2 300", "wait 15", "expect-session 2"]],
        },
        "expect": {"host": [r"cutscene player's step is Update",
                            r"Options pressed to skip it[\s\S]*cutscene player's step is Epilog"
                            r"[\s\S]*this game is ready, 1 of 1[\s\S]*load view is gone"]},
        # Unskipped, the opening held the screen ~115 s (21593 silent buffers): skipped, under
        # 10000 (about 53 s, the load and the join included).
        "console_expect": {"host": [r"Runtime: sound back after \d{1,4} silent buffers"]},
        "allow": CRASH_CLEANUP,
        "timeout": 900,
    },
    # Release thread 2026-10-16 (dog): the host travelled to the Dream before it saw the guest's
    # character; neither game said ready and the guest stayed covered (its input held) until the
    # host came back. A member on another map is not waited for: both screens open with the host
    # still in the Dream.
    "team-load-apart": {
        "about": "the host leaves for the Dream as soon as the team's session is up: neither "
                 "loading screen waits for it, both open with the host still away",
        "roles": {
            "host": ["team-create", "team-invite rig_guest", "team-wait 2", "team-start", START,
                     "wait-session 2 300", f"travel {HUNTERS_DREAM}", "wait 25",
                     "log apart-host", "wait 5"],
            "guest": ["team-accept", "team-ready", START, "wait-session 2 300", "wait 30",
                      "log apart-guest", "wait 5"],
        },
        "expect": {"host": [r"load view is gone[\s\S]*autoplay log: apart-host"],
                   "guest": [r"load view is gone[\s\S]*autoplay log: apart-guest"]},
        "forbid": {role: [r"load view is gone[\s\S]*load view is up"]
                   for role in ("host", "guest")},
        "timeout": 300,
    },
    # Kaniel 2026-10-15: the team's loading screen stays over the whole screen, the sound held,
    # until every guest has fully joined the host (its character spawned), then opens for all.
    # Kaniel 2026-10-17: the teammate compass. The host stands 5.2 m south of the guest in the
    # hallway north of Iosefka's door and turns. After a teleport with heading h the camera looks
    # along (sin h, -cos h) (rig 2026-10-17: h 0 looks south, -z; a quarter turn west, -x; half a
    # turn north): the guest behind the host, then on its right, then ahead. The guest, at h 0,
    # looks south at the host.
    "compass": {
        "about": "the teammate compass: each game's marker for the other is at the bearing and "
                 "distance where it stands as the camera sees it; the host shoots the strip",
        "roles": {
            "host": [START, "godmode", "wait-session 2 300", "wait 5",
                     f"teleport {COMPASS_HOST} 0.0", "wait 12", "expect-compass 180 5.2",
                     "shot host-compass-behind", f"teleport {COMPASS_HOST} 1.5708", "wait 4",
                     "expect-compass 90 5.2", "shot host-compass-right",
                     f"teleport {COMPASS_HOST} 3.1416", "wait 4", "expect-compass 0 5.2",
                     "shot host-compass-ahead", "wait 10"],
            "guest": [START, "godmode", "wait-session 2 300", "wait 8",
                      f"teleport {COMPASS_GUEST} 0.0", "wait 10", "expect-compass 0 5.2",
                      "wait 25"],
        },
        "expect": {role: [r"autoplay compass: camera heading"] for role in ("host", "guest")},
        "forbid": {"host": SESSION_HOLDS, "guest": SESSION_HOLDS},
        "timeout": 300,
    },
    # Kaniel 2026-10-17: a downed member's marker on the compass (the party list's downed badge,
    # its revive, over the others). Both at the lamp; the guest falls and stays down.
    "compass-down": {
        "about": "the teammate compass shows a downed member as down, with its revive; the host "
                 "shoots it",
        "settings": PARTY_DEATH,
        "roles": {
            "host": [START, "godmode", "wait-session 2 300", "wait-member-vital down 120",
                     "wait 2", "expect-compass 0 3 180 40 down", "shot host-compass-down",
                     "wait 5", "expect-session 2"],
            "guest": [START, "wait-session 2 300", "wait 15", *LETHAL_FALL,
                      "wait-vital down 30", "wait 20"],
        },
        "expect": {"host": [r"autoplay compass: camera heading .* down\]"]},
        "forbid": {"host": SESSION_HOLDS, "guest": SESSION_HOLDS},
        "timeout": 300,
    },
    "team-load-art": {
        "about": "the team loading screen's art (Kaniel 2026-10-17: the co-op art's night city, cards, "
                 "medallions and sigils): each game shoots its loading screen as it comes up",
        "roles": {
            "host": ["team-create", "team-invite rig_guest", "team-wait 2", "team-start", START, "shot host-loading",
                     "wait-session 2 300"],
            "guest": ["team-accept", "team-ready", START, "shot guest-loading", "wait-session 2 300"],
        },
        "expect": {role: [r"Co-op team load: the screen covers the game"] for role in ("host", "guest")},
        "timeout": 300,
    },
    "team-load-cover": {
        "about": "a team's load: each game's loading screen stays full screen with its sound "
                 "held until the other hunter's character stands in its world, then it opens "
                 "and the sound comes back",
        "roles": {
            "host": ["team-create", "team-invite rig_guest", "team-wait 2", "team-start", START,
                     "wait-session 2 300", "wait 30", "expect-session 2"],
            "guest": ["team-accept", "team-ready", START, "wait-session 2 300", "wait 30",
                      "expect-session 2"],
        },
        "expect": {role: [r"Co-op team load: the screen covers the game, its sound held",
                          r"Co-op team load: this game is ready, 1 of 1 other hunters' characters "
                          r"in its world[\s\S]*load view is gone",
                          r"Co-op team load: the screen opens, the sound back"]
                   for role in ("host", "guest")},
        "forbid": {role: SESSION_HOLDS + [r"load view is gone[\s\S]*load view is up"]
                   for role in ("host", "guest")},
        "console_expect": {role: [r"Runtime: sound held under the team's loading screen",
                                  r"Runtime: sound back after [1-9]\d* silent buffers"]
                           for role in ("host", "guest")},
        "timeout": 300,
    },
    # Phase 5b: the team's world. A world flag no save has (TEAM_WORLD_FLAG, Central Yharnam's
    # block, unused) set by the host in the team's first session is in the team's world; the
    # guest's save takes it at its next team load, and loses it again when it plays alone (its own
    # world back).
    "team-world": {
        "about": "the team's world: the host's save makes it, the guest's save takes it in the "
                 "next session, and the guest alone has its own world back",
        "roles": {
            "host": [["team-create", "team-invite rig_guest", "team-wait 2", "team-start", START,
                      "wait-session 2 300", f"set-flag {TEAM_WORLD_FLAG} 1", "wait 15", "done"],
                     ["team-create", "team-invite rig_guest", "team-wait 2", "team-start", START,
                      "wait-session 2 300", f"expect-flag {TEAM_WORLD_FLAG} 1",
                      "wait 60",
                      "done"]],
            "guest": [["team-accept", "team-ready", START, "wait-session 2 300",
                       f"expect-flag {TEAM_WORLD_FLAG} 0", "wait 45", "done"],
                      ["team-accept", "team-ready", START, "wait-session 2 300",
                       f"expect-flag {TEAM_WORLD_FLAG} 1",
                       "done"],
                      ["team-leave", "team-alone", START, "wait 10",
                       f"expect-flag {TEAM_WORLD_FLAG} 0"]],
        },
        "expect": {
            "host": [r"Co-op team world: this host's world to the server"],
            "guest": [r"Co-op team world: the team's world is in this save",
                      r"Co-op team world: slot \d+'s own world kept aside",
                      r"Co-op team world: slot \d+ plays alone: its own world is back"],
        },
        "allow": CRASH_CLEANUP,
        "timeout": 900,
    },
    "team-screen": {
        "about": "the team screen after Continue (run drawn: pictures): a team of two forms, the "
                 "guest readies, the host begins",
        "roles": {
            "host": ["team-create", "team-invite rig_guest", "team-wait 2", "team-screen",
                     "title-wait 3", "title-shot screen-host", "title-wait 10",
                     "title-shot screen-host-ready", "team-start", START, "wait-session 2 300",
                     "snapshot"],
            "guest": ["team-accept", "team-screen", "title-wait 2", "title-shot screen-guest",
                      "team-ready", "title-wait 2", "title-shot screen-guest-ready", START,
                      "wait-session 2 300", "snapshot"],
        },
        "timeout": 300,
    },
    "team-join3": {
        "about": "a team of three from the lobby: the host invites both guests",
        "roles": {
            "host": ["team-create", "team-invite rig_guest", "team-invite rig_guest2",
                     "team-wait 3", "team-start", START, "godmode", "wait-session 3 480", "wait 10",
                     "snapshot", "expect-session 3"],
            "guest": ["team-accept", "team-ready", START, "godmode", "wait-session 3 480", "wait 10",
                      "snapshot", "expect-session 3"],
            "guest2": ["team-accept", "team-ready", START, "godmode", "wait-session 3 480", "wait 10",
                       "snapshot", "expect-session 3"],
        },
        "expect": {
            "host": [r"Co-op team lobby: the save plays in team \w+ as its host"],
            "guest": [r"Co-op team lobby: the save plays in team \w+ as its guest"],
            "guest2": [r"Co-op team lobby: the save plays in team \w+ as its guest"],
        },
        "timeout": 300,
    },
    # Kaniel 2026-10-16: the team menu in game (the System menu's Co-op Team page). The leader
    # removes a member while both play: it leaves the session where it stands and plays on alone
    # (no title, no reload); the host plays on with its session open.
    "team-menu-kick": {
        "about": "in game, the host removes the guest from the team: the guest leaves the session "
                 "where it stands and plays alone, the host plays on",
        "roles": {
            "host": [START, "godmode", "wait-session 2 300", "wait 10", "menu-kick rig_guest 60",
                     "wait-solo 120", "wait 10", "snapshot", f"expect-map {CENTRAL_YHARNAM}"],
            "guest": [START, "godmode", "wait-session 2 300", "wait-solo 120", "wait 15",
                      "snapshot", f"expect-map {CENTRAL_YHARNAM}", "wait 15", "snapshot",
                      f"expect-map {CENTRAL_YHARNAM}"],
        },
        "expect": {
            "guest": [r"Co-op team menu: out of the team \(world:\w+\): left its session"],
        },
        # The removed guest's own leave (MultiPlayStopRequest where it stands) is the point.
        "allow": [r"left the session outside a trip"],
        "timeout": 300,
    },
    # A save playing alone is invited from the leader's menu and joins the hunt from its own,
    # where it stands, without going back to the title.
    # A member leaves the team from the menu while playing: out of the session where it stands, it
    # plays on alone; the host plays on with its session open.
    # A save played alone while another game starts a team loads into its world (rig 2026-10-16:
    # with the team menu's first build its load hung 3 times in 7, the game's flag read run every
    # frame of it).
    "team-solo-load": {
        "about": "the host starts a team alone and the guest plays alone: the guest's load gets "
                 "into its world",
        "roles": {
            "host": ["team-create", "team-start", START, "wait 20", "snapshot"],
            "guest": ["team-alone", START, "wait 10", "snapshot"],
        },
        "timeout": 200,
    },
    "team-menu-leave": {
        "about": "in game, the guest leaves the team from the menu: it leaves the session where it "
                 "stands and plays alone, the host plays on",
        "roles": {
            "host": [START, "godmode", "wait-session 2 300", "wait-solo 120", "wait 10",
                     "snapshot", f"expect-map {CENTRAL_YHARNAM}"],
            "guest": [START, "godmode", "wait-session 2 300", "wait 10", "menu-leave 60",
                      "wait-solo 60", "wait 15", "snapshot", f"expect-map {CENTRAL_YHARNAM}"],
        },
        "expect": {
            "guest": [r"Co-op team menu: out of the team \(world:\w+\): left its session"],
        },
        "allow": [r"left the session outside a trip"],
        "timeout": 300,
    },
    # Kaniel 2026-10-16: the team menu from the Insert menu too (its "Co-op Team" row, the last
    # but Quit): the team screen opens over the game and its Close goes back to the game. The
    # team's loading screen lingers a while after the join (the menu does not open under it).
    "team-menu-insert": {
        "about": "in game, the Insert menu's Co-op Team row opens the team screen; its Close "
                 "closes it again, the session holds",
        "roles": {
            "host": [START, "godmode", "wait-session 2 300", "wait 30", "wait 20", "snapshot",
                     "expect-session 2"],
            "guest": [START, "godmode", "wait-session 2 300", "wait 25", "keys toggle up up enter",
                      "wait 3", "shot team-menu", "keys enter", "wait 3", "snapshot", "expect-session 2"],
        },
        "expect": {
            "guest": [r"Co-op team menu: the team screen is open from the port's Insert menu",
                      r"Co-op team menu: the team screen is closed back to the port's Insert menu"],
        },
        "forbid": {"guest": SESSION_HOLDS},
        "timeout": 300,
    },
    # Kaniel 2026-10-16: a removed member invited back joins again from the menu where it stands
    # (its game's second join crashed at eboot+0x13cffba until the flag block guard).
    "team-menu-rejoin": {
        "about": "in game, the host removes the guest, then invites it back: the guest joins the "
                 "hunt again from the menu, no title, no crash",
        "roles": {
            "host": [START, "godmode", "wait-session 2 300", "wait 10", "menu-kick rig_guest 60",
                     "wait-solo 120", "wait 10", "menu-invite rig_guest 120",
                     "wait-session 2 120", "wait 20", "snapshot", "expect-session 2"],
            "guest": [START, "godmode", "wait-session 2 300", "wait-solo 120", "wait 10",
                      "menu-join 120", "wait-session 2 120", "wait 20", "snapshot",
                      "expect-session 2", f"expect-map {CENTRAL_YHARNAM}"],
        },
        "expect": {
            "guest": [r"Co-op team menu: out of the team \(world:\w+\): left its session",
                      r"Co-op team menu: this save joins team \w+ where it stands, as its guest"],
        },
        "allow": [r"left the session outside a trip"],
        "timeout": 300,
    },
    "team-menu-join": {
        "about": "in game, the host invites a guest playing alone; the guest joins the hunt from "
                 "the menu where it stands",
        "roles": {
            "host": ["team-create", "team-start", START, "godmode", "wait 15",
                     "menu-invite rig_guest 240", "wait-session 2 300", "wait 10", "snapshot",
                     "expect-session 2"],
            "guest": ["team-alone", START, "godmode", "wait 10", "menu-join 240",
                      "wait-session 2 300", "wait 10", "snapshot", "expect-session 2",
                      f"expect-map {CENTRAL_YHARNAM}"],
        },
        "expect": {
            "guest": [r"Co-op team menu: this save joins team \w+ where it stands, as its guest"],
        },
        "timeout": 300,
    },
    "join3": {
        "about": "a party of three: both guests join the host (one bell summons both)",
        "roles": {
            "host": [START, "godmode", "wait-session 3 480", "wait 10", "snapshot",
                     "expect-session 3"],
            "guest": [START, "godmode", "wait-session 3 480", "wait 10", "snapshot",
                      "expect-session 3"],
            "guest2": [START, "godmode", "wait-session 3 480", "wait 10", "snapshot",
                       "expect-session 3"],
        },
        "timeout": 300,
    },
    # Kaniel 2026-10-15: between two guests, one guest's actions played on the other guest's own
    # character (a pick-up's gesture, a backstab's teleport, levers). The second guest stands
    # still; its census shows where its own character is before and after the first one acts.
    "guest-acts-for-two": {
        "about": "a party of three: the first guest picks up an item and attacks an enemy while "
                 "the second guest stands still; the second guest's own character stays put",
        "roles": {
            "host": [START, "godmode", "wait-session 3 480", "wait 70", "expect-session 3"],
            "guest": [START, "godmode", "wait-session 3 480", "wait 15", "pickup 60", "wait 5",
                      *attack(ENEMY), "wait 10", "expect-session 3"],
            "guest2": [START, "godmode", "wait-session 3 480", "wait 10", "census 5", "wait 20",
                       "census 5", "wait 25", "census 5", "expect-session 3"],
        },
        "stays": {"guest2": 3.0},
        "timeout": 900,
    },
    # Kaniel 2026-10-15: the host and a guest went to the Hunter's Dream and the second guest still
    # saw them in Central Yharnam. It sees neither while they are away, both once they are back.
    "away-unseen3": {
        "about": "a party of three: the host and the first guest go to the Hunter's Dream; the "
                 "second guest shows neither of them meanwhile, and both again once they are back",
        "roles": {
            "host": [START, "godmode", "wait-session 3 480", "wait 20", f"travel {HUNTERS_DREAM}",
                     "wait 60", f"travel {CENTRAL_YHARNAM_LAMP}", "wait-members 3 240", "wait 30",
                     "expect-session 3"],
            "guest": [START, "godmode", "wait-session 3 480", "wait 25",
                      f"travel {HUNTERS_DREAM}", "wait 55", f"travel {CENTRAL_YHARNAM_LAMP}",
                      "wait-members 3 240", "wait 30", "expect-session 3"],
            # Both away from about 40 s on, back after about 85 s.
            "guest2": [START, "godmode", "wait-session 3 480", "wait 15", "expect-others-shown 2",
                       "wait 40", "expect-others-shown 0", "wait-members 3 240", "wait 20",
                       "expect-others-shown 2", "expect-session 3"],
        },
        "timeout": 1000,
    },
    "join3-uncap": {
        "about": "join3 at the kit's frame rate (Uncap FPS++): rig 2026-10-12, the host saw the "
                 "second guest's sign and never claimed it (3 runs of 3; at 30 FPS it joined)",
        "roles": {
            "host": [START, "godmode", "wait-session 3 480", "wait 10", "snapshot",
                     "expect-session 3"],
            "guest": [START, "godmode", "wait-session 3 480", "wait 10", "snapshot",
                      "expect-session 3"],
            "guest2": [START, "godmode", "wait-session 3 480", "wait 10", "snapshot",
                       "expect-session 3"],
        },
        "settings": {"BB_FPS": "uncap"},
        "timeout": 900,
    },
    "trip3": {
        "about": "a party of three apart (everyone sees both others at the end): the second guest waits in the Dream while the host "
                 "goes there and back; the host re-creates only the guest on its map, then the "
                 "second guest when it comes back",
        "roles": {
            "host": [START, "godmode", "wait-session 3 480", "wait 40", f"travel {HUNTERS_DREAM}",
                     "wait 5", f"travel {CENTRAL_YHARNAM_LAMP}", "wait 60", "wait-members 3 300",
                     "wait 20", "snapshot", "expect-session 3", "expect-others-near 2 15"],
            "guest": [START, "godmode", "wait-session 3 480", "wait 150", "wait-members 3 300",
                      "wait 25", "snapshot", "expect-session 3", "expect-others-near 2 15"],
            "guest2": [START, "godmode", "wait-session 3 480", "wait 10",
                       f"travel {HUNTERS_DREAM}", "wait 90", f"travel {CENTRAL_YHARNAM_LAMP}",
                       "wait-members 3 300", "wait 25", "snapshot", "expect-session 3",
                       "expect-others-near 2 15"],
        },
        "expect": {
            "host": [r"re-creating rig_guest on map 0x18010000 \(1 of 2 members\)",
                     # Since PR #41 both may be named in one line ("rig_guest rig_guest2").
                     r"re-creating (?:\S+ )*rig_guest2 (?:\S+ )*on map 0x18010000"],
        },
        "timeout": 1200,
    },
    "guest-trip3": {
        "about": "a party of three: one guest goes to the Dream and back; afterwards both guests "
                 "and the host see each other (Kaniel 2026-10-09: guests did not see each other "
                 "after travelling)",
        "roles": {
            "host": [START, "godmode", "wait-session 3 480", "wait 150", "wait-members 3 300",
                     "wait 25", "snapshot", "expect-session 3", "expect-others-near 2 15"],
            "guest": [START, "godmode", "wait-session 3 480", "wait 20",
                      f"travel {HUNTERS_DREAM}", "wait 20", f"travel {CENTRAL_YHARNAM_LAMP}",
                      "wait-members 3 300", "wait 25", "snapshot", "expect-session 3",
                      "expect-others-near 2 15"],
            "guest2": [START, "godmode", "wait-session 3 480", "wait 150", "wait-members 3 300",
                       "wait 25", "snapshot", "expect-session 3", "expect-others-near 2 15"],
        },
        "timeout": 1200,
    },
    "guest-talk-at-lamp": {
        "about": "a guest in the session presses X at the first Central Yharnam lamp, where "
                 "Gilbert's window is (Kaniel 2026-10-09: the 'Talk' prompt took the guest to "
                 "the Dream through the lamp); the talk log shows which script answered",
        "roles": {
            "host": [START, "godmode", "wait-session 2 300", "wait 80", "snapshot",
                     "expect-session 2"],
            "guest": [START, "godmode", "wait-session 2 300", "wait 15", "snapshot",
                      "press cross 200", "wait 8", "press cross 200", "wait 8",
                      "press circle 200", "wait 8", "snapshot", f"expect-map {CENTRAL_YHARNAM}",
                      "expect-session 2"],
        },
        "timeout": 600,
    },
    "guest-lamp-rest": {
        "about": "a guest in the session uses the Cathedral Ward lamp (Oedon Chapel) with X and rests "
                 "there with the party (playtest 2026-10-13: guests could not use lamps in the Ward "
                 "or after Father Gascoigne); the host first, as the control",
        "roles": {
            "host": [START, f"ensure-map {CATHEDRAL_WARD} {CATHEDRAL_WARD_LAMP}", "godmode",
                     "wait-session 2 420", "wait 5", *USE_WARD_LAMP("host"), "wait 200", "snapshot",
                     "expect-session 2"],
            "guest": [START, f"ensure-map {CATHEDRAL_WARD} {CATHEDRAL_WARD_LAMP}", "godmode",
                      "wait-session 2 420", "wait 160", *USE_WARD_LAMP("guest"), "snapshot",
                      "expect-session 2"],
        },
        # The lamp's script (m24_00 t240000, states 1-4) takes X only while ESD functions 14, 19,
        # 26, 27 and 31 answer 0; 26 answered 1 for a guest. The rig's X at the lamp brings no
        # prompt even for the host (2026-10-14, 18 aims from both sides), so the check is the
        # guest's answers: 26 answers 0 as the host's, never 1.
        "expect": {"guest": [r"guest talk answers: .*\bf26=0x", r"\bf14=0x.*\bf19=0x"]},
        "forbid": {"host": SESSION_HOLDS, "guest": SESSION_HOLDS + [r"\bf26=1x"]},
        "timeout": 1000,
    },
    "ward-door": {
        "about": "the guest walks out of Cathedral Ward to Central Yharnam and back three times "
                 "(playtest 2026-10-13: after going back and forth through that door the guests "
                 "could not use the Ward lamp or hurt the NPC by it), then uses the Ward lamp",
        "roles": {
            "host": [START, f"ensure-map {CATHEDRAL_WARD} {CATHEDRAL_WARD_LAMP}", "godmode",
                     "wait-session 2 420", "wait 420", "census 60", "snapshot", "expect-session 2"],
            "guest": [START, f"ensure-map {CATHEDRAL_WARD} {CATHEDRAL_WARD_LAMP}", "godmode",
                      "wait-session 2 420", "wait 15",
                      *[step for _ in range(3) for step in (
                          f"glide {WARD_TO_YHARNAM}", "wait 12", f"glide {YHARNAM_TO_WARD}",
                          "wait 12")],
                      f"expect-map {CATHEDRAL_WARD}", *USE_WARD_LAMP("guest"), "census 60",
                      "snapshot", "expect-session 2"],
        },
        "expect": {"guest": [r"map change 0x18000000 -> 0x18010000",
                             r"guest talk answers: .*\bf26=0x"]},
        "forbid": {"host": SESSION_HOLDS, "guest": SESSION_HOLDS + [r"\bf26=1x"]},
        "timeout": 1100,
    },
    "pickup": {
        "about": "host and guest each pick up a map item with X in a session (the host could not "
                 "pick up items in the Forbidden Woods, 2026-10-09)",
        "roles": {
            "host": [START, "godmode", "wait-session 2 300", "wait 10", "pickup 60", "wait 5",
                     "snapshot", "expect-session 2"],
            "guest": [START, "godmode", "wait-session 2 300", "wait 30", "pickup 60", "wait 5",
                      "snapshot", "expect-session 2"],
        },
        "timeout": 300,
    },
    "pickup-after-crash": {
        "about": "after the guest's game crashes and it joins again, both still pick up map items "
                 "with X (friend run 2026-10-09: no pickups after a crash); the item by the lamp: "
                 "the next one is 21 m down, and a jump there drops the player through the floor "
                 "and crashes the game in 0x0191B6E0 (twice with the guest back, twice without)",
        "roles": {
            "host": [START, "godmode", "wait-session 2 300", "wait-solo 120",
                     "wait-session 2 420", "wait 15", "pickup 20", "wait 5", "snapshot",
                     "expect-session 2"],
            "guest": [[START, "godmode", "wait-session 2 300", "wait 15", "done"],
                      [START, "godmode", "wait-session 2 420", "wait 40", "pickup 20", "wait 5",
                       "snapshot", "expect-session 2"]],
        },
        "allow": CRASH_CLEANUP,
        "timeout": 900,
    },
    "pickup-after-leave": {
        "about": "the guest's game goes away and the host, alone now, picks up a map item with X "
                 "before anyone joins again (the leave's 'load wait' stayed on: Kaniel "
                 "2026-10-11, no pickups or talks after a friend left)",
        "roles": {
            "host": [START, "godmode", "wait-session 2 300", "wait-solo 120", "wait 10",
                     "pickup 20", "wait 3", "snapshot"],
            "guest": [[START, "godmode", "wait-session 2 300", "wait 15", "done"],
                      ["wait 1", "done"]],
        },
        "expect": {"host": [r"lost a member; staying where it is"]},
        "allow": CRASH_CLEANUP,
        "timeout": 300,
    },
    "pickup-after-leave3": {
        "about": "a party of three: one guest's game goes away; the host and the other guest "
                 "still pick up map items with X (Kaniel 2026-10-11: after a friend left neither "
                 "could pick up or talk, only fight)",
        "roles": {
            "host": [START, "godmode", "wait-session 3 480", "wait 20", "wait-session 2 120",
                     "wait 10", "pickup 20", "wait 3", "snapshot"],
            "guest": [START, "godmode", "wait-session 3 480", "wait 20", "wait-session 2 120",
                      "wait 10", "pickup 20", "wait 3", "snapshot"],
            "guest2": [[START, "godmode", "wait-session 3 480", "wait 10", "done"],
                       ["wait 1", "done"]],
        },
        "expect": {role: [r"lost a member; staying where it is"] for role in ("host", "guest")},
        "allow": CRASH_CLEANUP,
        "timeout": 300,
    },
    "guest-talk-by-the-lamp": {
        "about": "a guest in the session presses X on each side of the character beside the first "
                 "Central Yharnam lamp (Gilbert's window), turned towards it; it must not take the "
                 "lamp to the Dream (Kaniel 2026-10-09: 'Talk' there used the lamp)",
        "roles": {
            "host": [START, "godmode", "wait-session 2 300", "wait 140", "snapshot",
                     "expect-session 2"],
            "guest": [START, "godmode", "wait-session 2 300", "wait 10", "snapshot",
                      f"approach {BY_THE_LAMP} 0 1.2 0", "wait 3", "snapshot", "press cross 200", "wait 6", "press circle 200", "wait 4", "snapshot",
                      f"expect-map {CENTRAL_YHARNAM}",
                      f"approach {BY_THE_LAMP} 0 -1.2 3.14", "wait 3", "snapshot", "press cross 200", "wait 6", "press circle 200", "wait 4", "snapshot",
                      f"expect-map {CENTRAL_YHARNAM}",
                      f"approach {BY_THE_LAMP} 1.2 0 1.57", "wait 3", "snapshot", "press cross 200", "wait 6", "press circle 200", "wait 4", "snapshot",
                      f"expect-map {CENTRAL_YHARNAM}",
                      f"approach {BY_THE_LAMP} 1.2 0 -1.57", "wait 3", "snapshot", "press cross 200", "wait 6", "press circle 200", "wait 4", "snapshot",
                      f"expect-map {CENTRAL_YHARNAM}",
                      f"approach {BY_THE_LAMP} -1.2 0 -1.57", "wait 3", "snapshot", "press cross 200", "wait 6", "press circle 200", "wait 4", "snapshot",
                      f"expect-map {CENTRAL_YHARNAM}",
                      f"approach {BY_THE_LAMP} -1.2 0 1.57", "wait 3", "snapshot", "press cross 200", "wait 6", "press circle 200", "wait 4", "snapshot",
                      f"expect-map {CENTRAL_YHARNAM}",
                      "expect-session 2"],
        },
        "timeout": 700,
    },
    "pickup-after-trip": {
        "about": "after a trip that keeps the session (the host to the Dream and back, the guest "
                 "to the Dream and back) both still pick up the item by the lamp with X (Kaniel "
                 "2026-10-09: no pickups in the Forbidden Woods after the time-of-day reload)",
        "roles": {
            "host": [START, "godmode", "wait-session 2 300", "wait 10", f"travel {HUNTERS_DREAM}",
                     "wait 5", f"travel {CENTRAL_YHARNAM_LAMP}", "wait-members 2 120", "wait 15",
                     "pickup 20", "wait 5", "snapshot", "expect-session 2"],
            "guest": [START, "godmode", "wait-session 2 300", "wait 90", f"travel {HUNTERS_DREAM}",
                      "wait 10", f"travel {CENTRAL_YHARNAM_LAMP}", "wait-members 2 240",
                      "wait 15", "pickup 20", "wait 5", "snapshot", "expect-session 2"],
        },
        "forbid": {"host": SESSION_HOLDS, "guest": SESSION_HOLDS},
        "timeout": 720,
    },
    "pickup-after-reload": {
        "about": "after each one's map reloads in place in the session (a warp to the lamp it "
                 "stands at, as the time-of-day reload Kaniel had before the Forbidden Woods "
                 "pickups stopped, 2026-10-09) both still pick up the item by the lamp with X",
        "roles": {
            "host": [START, "godmode", "wait-session 2 300", "wait 10",
                     f"travel {CENTRAL_YHARNAM_LAMP}", "wait-members 2 120", "wait 15",
                     "pickup 20", "wait 5", "snapshot", "expect-session 2"],
            "guest": [START, "godmode", "wait-session 2 300", "wait 80",
                      f"travel {CENTRAL_YHARNAM_LAMP}", "wait-members 2 240", "wait 15",
                      "pickup 20", "wait 5", "snapshot", "expect-session 2"],
        },
        "forbid": {"host": SESSION_HOLDS, "guest": SESSION_HOLDS},
        "timeout": 720,
    },
    "guest-talk-gilbert": {
        "about": "at the second Central Yharnam lamp (talk 241001) a guest presses X at the window "
                 "3 m from it (talk 241319, Gilbert's), turned towards it; it must not take the lamp "
                 "to the Dream (Kaniel 2026-10-09: 'Talk' there used the lamp)",
        "roles": {
            # The host stays: its warp to another lamp of its map made the guest's game drop the
            # room for a moment (rig 2026-10-10, as pickup-after-reload's in-place warp).
            "host": [START, "godmode", "wait-session 2 300", "wait 230", "snapshot",
                     "expect-session 2"],
            "guest": [START, "godmode", "wait-session 2 300", "wait 20",
                      f"travel {CENTRAL_YHARNAM_LAMP_2}", "wait 15",
                      "snapshot",
                      "teleport -198.45 -28.4 63.96 2.29", "wait 2", "teleport -198.45 -28.4 63.96 2.29", "wait 3", "snapshot",
                      "press cross 200", "wait 6", "snapshot", "press circle 200", "wait 4",
                      f"expect-map {CENTRAL_YHARNAM}",
                      "teleport -198.45 -28.4 63.96 2.29", "wait 2", "teleport -198.45 -28.4 63.96 2.29", "wait 3", "snapshot",
                      "press cross 200", "wait 6", "snapshot", "press circle 200", "wait 4",
                      f"expect-map {CENTRAL_YHARNAM}",
                      "teleport -197.6 -28.4 64.7 2.29", "wait 2", "teleport -197.6 -28.4 64.7 2.29", "wait 3", "snapshot",
                      "press cross 200", "wait 6", "snapshot", "press circle 200", "wait 4",
                      f"expect-map {CENTRAL_YHARNAM}",
                      "teleport -198.45 -28.4 63.96 2.6", "wait 2", "teleport -198.45 -28.4 63.96 2.6", "wait 3", "snapshot",
                      "press cross 200", "wait 6", "snapshot", "press circle 200", "wait 4",
                      f"expect-map {CENTRAL_YHARNAM}",
                      "teleport -198.45 -28.4 63.96 2.0", "wait 2", "teleport -198.45 -28.4 63.96 2.0", "wait 3", "snapshot",
                      "press cross 200", "wait 6", "snapshot", "press circle 200", "wait 4",
                      f"expect-map {CENTRAL_YHARNAM}",
                      "expect-session 2"],
        },
        "timeout": 900,
    },
    "hp-after-host-trip": {
        "about": "after the host's trip to the Dream and back, enemy HP still goes both ways: the "
                 "host's hit reaches the guest's copy and the guest's real hit the host's (Kaniel "
                 "2026-10-10: HP not shared after trips, he thinks)",
        "roles": {
            "host": [START, "godmode", "wait-session 2 300", "wait 10", f"travel {HUNTERS_DREAM}",
                     "wait 5", f"travel {CENTRAL_YHARNAM_LAMP}", "wait-members 2 120", "wait 20",
                     f"hurt {ENEMY} 300", "wait 40", "snapshot", f"expect-hp {ENEMY} < 300",
                     "expect-session 2"],
            "guest": [START, "godmode", "wait-session 2 300", "wait 45", "wait-members 2 240",
                      "wait 30", "snapshot", f"expect-hp {ENEMY} <= 300", f"hit {ENEMY}",
                      "wait 10", "snapshot", "expect-session 2"],
        },
        "forbid": {"host": SESSION_HOLDS, "guest": SESSION_HOLDS},
        "timeout": 720,
    },
    "hp-after-guest-trip": {
        "about": "after the guest's trip to the Dream and back, enemy HP still goes both ways: the "
                 "host's hit reaches the guest's copy and the guest's real hit the host's",
        "roles": {
            # The host hits once the guest is back (its wait-members), the guest looks 14 s
            # later and then hits (the t= on step lines puts both logs on one clock).
            "host": [START, "godmode", "wait-session 2 300", "wait 60", "wait-members 2 240",
                     "wait 20", f"hurt {ENEMY} 300", "wait 60", "snapshot",
                     f"expect-hp {ENEMY} < 300", "expect-session 2"],
            "guest": [START, "godmode", "wait-session 2 300", "wait 10", f"travel {HUNTERS_DREAM}",
                      "wait 10", f"travel {CENTRAL_YHARNAM_LAMP}", "wait-members 2 240",
                      "wait 60", "snapshot", f"expect-hp {ENEMY} <= 300", f"hit {ENEMY}",
                      "wait 10", "snapshot", "expect-session 2"],
        },
        "forbid": {"host": SESSION_HOLDS, "guest": SESSION_HOLDS},
        "timeout": 720,
    },
    "dead-before-session": {
        "about": "an enemy the host killed before the guest joined stays dead for the guest after "
                 "its trip to the Dream and back, in its own world (Kaniel 2026-10-11: the "
                 "guests saw enemies dead in the host's world alive again)",
        "roles": {
            "host": [START, "godmode", f"kill {ENEMY}", "wait-session 2 300", "wait 60",
                     "wait-members 2 240", "wait 40", "snapshot", "expect-session 2"],
            "guest": [START, "godmode", "wait-session 2 300", "wait 10", f"travel {HUNTERS_DREAM}",
                      "wait 10", f"travel {CENTRAL_YHARNAM_LAMP}", "wait-members 2 240",
                      "wait 10", f"expect-hidden {ENEMY} 1 30", "snapshot", "expect-session 2"],
        },
        "forbid": {"host": SESSION_HOLDS, "guest": SESSION_HOLDS},
        "timeout": 720,
    },
    "guest-hit-once": {
        "about": "after both players' trips each runs its own copy of the map; the guest's real "
                 "hits reach the host's copy through the game and count once: the host does not "
                 "post them back, and both end at max HP less the guest's hits (Kaniel's friend "
                 "2026-10-11: each hit repeated a split second later)",
        "roles": {
            "host": [START, "godmode", "wait-session 2 300", "wait 10", f"travel {HUNTERS_DREAM}",
                     "wait 5", f"travel {CENTRAL_YHARNAM_LAMP}", "wait-members 2 120", "wait 120",
                     "census 40", "expect-session 2"],
            "guest": [START, "godmode", "wait-session 2 300", "wait 45", "wait-members 2 240",
                      "wait 5", f"travel {HUNTERS_DREAM}", "wait 5", f"travel {CENTRAL_YHARNAM_LAMP}",
                      "wait-members 2 240", "wait 20", f"hit {ENEMY} 30", "wait 1",
                      f"hit {ENEMY} 30", "wait 20", "census 40", "expect-session 2"],
        },
        "same_hp": ENEMY,
        "hp_sums_hits": ENEMY,
        "expect": {"host": [HP_SHARED],
                   "guest": [r"hit on 0x1001400b by this player",
                             HP_SHARED]},
        "forbid": {"host": SESSION_HOLDS, "guest": SESSION_HOLDS},
        "timeout": 720,
    },
    "hit-after-host-trip": {
        "about": "only the host went to the Dream and back: the guest's hits must still reach "
                 "the host's copy (rig 2026-10-11: the guest stayed in 'one world' mode, its "
                 "copy went back to the host's full HP and its hits were lost)",
        "roles": {
            "host": [START, "godmode", "wait-session 2 300", "wait 10", f"travel {HUNTERS_DREAM}",
                     "wait 5", f"travel {CENTRAL_YHARNAM_LAMP}", "wait-members 2 120", "wait 90",
                     "census 40", "expect-session 2"],
            "guest": [START, "godmode", "wait-session 2 300", "wait 45", "wait-members 2 240",
                      "wait 30", f"hit {ENEMY} 30", "wait 1", f"hit {ENEMY} 30", "wait 20",
                      "census 40", "expect-session 2"],
        },
        "same_hp": ENEMY,
        "hp_sums_hits": ENEMY,
        "forbid": {"host": SESSION_HOLDS, "guest": SESSION_HOLDS},
        "timeout": 720,
    },
    "hit3": {
        "about": "a party of three, each back from its own trip (worlds apart): all three hit "
                 "one enemy in turn, and every game ends at its max HP less the three hits, each "
                 "counted once (Kaniel 2026-10-11)",
        "roles": {
            "host": [START, "godmode", "wait-session 3 480", "wait 10", f"travel {HUNTERS_DREAM}",
                     "wait 5", f"travel {CENTRAL_YHARNAM_LAMP}", "wait-members 3 300", "wait 100",
                     f"hit {ENEMY} 30", "wait 70", "census 40", "expect-session 3"],
            "guest": [START, "godmode", "wait-session 3 480", "wait 45", "wait-members 3 300",
                      "wait 5", f"travel {HUNTERS_DREAM}", "wait 5", f"travel {CENTRAL_YHARNAM_LAMP}",
                      "wait-members 3 300", "wait 60", f"hit {ENEMY} 30", "wait 55",
                      "census 40", "expect-session 3"],
            "guest2": [START, "godmode", "wait-session 3 480", "wait 45", "wait-members 3 300",
                       "wait 25", f"travel {HUNTERS_DREAM}", "wait 5",
                       f"travel {CENTRAL_YHARNAM_LAMP}", "wait-members 3 300", "wait 75",
                       f"hit {ENEMY} 30", "wait 45", "census 40", "expect-session 3"],
        },
        "same_hp": ENEMY,
        "hp_sums_hits": ENEMY,
        # From a hit to the other games' bars, on average (0.47 s on a 200 ms board round, 0.30 s
        # posting on the hit with 100 ms rounds, then straight from game to game over the P2P
        # link; forest run 2026-10-11: the host's two-hit kills ended before the guests' bars
        # moved).
        "max_hp_lag": 0.15,
        "expect": {"host": [HP_SHARED]},
        "forbid": {"host": SESSION_HOLDS, "guest": SESSION_HOLDS, "guest2": SESSION_HOLDS},
        "timeout": 1200,
    },
    "crash-rejoin-hp": {
        "about": "after the guest's game crashes and it joins again, the host's hit still reaches "
                 "the guest's copy of an enemy "
                 "(friend run 2026-10-09: HP bars apart after a crash)",
        "roles": {
            "host": [START, "godmode", "wait-session 2 300", "wait-solo 120",
                     "wait-session 2 420", "wait 20", f"hurt {ENEMY} 300", "wait 40", "snapshot",
                     "expect-session 2"],
            "guest": [[START, "godmode", "wait-session 2 300", "wait 15", "done"],
                      [START, "godmode", "wait-session 2 420", "wait 35", "snapshot",
                       f"expect-hp {ENEMY} <= 300", "expect-session 2"]],
        },
        # In a session the host's game owns its enemies' HP and sends it to the guest itself (a
        # guest writing its copy's HP is put back within seconds), so only the host's hit is
        # checked here; the boards cover members apart (hp-apart).
        "allow": CRASH_CLEANUP,
        "timeout": 900,
    },
    "owner-leaves": {
        "about": "phase 4: the host runs Central Yharnam's enemies and hurts one; it goes to the "
                 "Dream and the guest takes the area over with the enemy as hurt as it was (no "
                 "reset, no full HP), kills it, and the host coming back finds it dead",
        "roles": {
            "host": [START, "godmode", "wait-session 2 300", "wait 20", f"hurt {ENEMY} 200",
                     "wait 10", "sync hurt", f"travel {HUNTERS_DREAM}", "sync killed", "wait 5",
                     f"travel {CENTRAL_YHARNAM_LAMP}", "wait-members 2 240", "wait 20",
                     "snapshot"],
            "guest": [START, "godmode", "wait-session 2 300", "sync hurt", "wait 5",
                      f"expect-hp {ENEMY} <= 200", "wait 20", f"expect-hp {ENEMY} <= 200",
                      *attack(ENEMY), "wait 5", "sync killed", "wait-members 2 240", "wait 20",
                      "snapshot"],
        },
        "syncs": {"hurt": {"chapter": "owner-leaves", "roles": ["host", "guest"]},
                  "killed": {"chapter": "owner-leaves", "roles": ["host", "guest"]}},
        "expect": {"host": [OWNER_RUNS,
                            r"(?:hid 1 enemies a member killed on map 0x18010000|"
                            r"0x18010000, 1 near died)"],
                   "guest": [OWNER_FOLLOWS, OWNER_RUNS]},
        "forbid": {"host": SESSION_HOLDS, "guest": SESSION_HOLDS},
        "timeout": 300,
    },
    "owner-crash": {
        "about": "phase 4: the guest runs Central Yharnam's enemies (the host went to the Dream and "
                 "came back to follow it); the guest's game crashes and the host takes the area "
                 "over: its own hit lands",
        "roles": {
            "host": [START, "godmode", "wait-session 2 300", "wait 10", f"travel {HUNTERS_DREAM}",
                     "wait 10", f"travel {CENTRAL_YHARNAM_LAMP}", "wait-members 2 240",
                     "wait 20", "sync back", "wait 30", f"hit {ENEMY} 30", "wait 5",
                     f"expect-hp {ENEMY} < 484", "snapshot"],
            "guest": [[START, "godmode", "wait-session 2 300", "wait 20", "wait-members 2 240",
                       "wait 20", "sync back", "wait 2", "done"],
                      [START, "wait 20", "snapshot"]],
        },
        "syncs": {"back": {"chapter": "owner-crash", "roles": ["host", "guest"]}},
        "expect": {"host": [OWNER_FOLLOWS, OWNER_RUNS], "guest": [OWNER_RUNS]},
        "allow": CRASH_CLEANUP,
        "timeout": 900,
    },
    "border-fight": {
        "about": "phase 4: the guest joins in place in Cathedral Ward while the host is in Central "
                 "Yharnam, two blocks of one area: each runs its own block's enemies, then the "
                 "guest crosses to the host's block and follows it there; both hits on one enemy "
                 "count once in both games",
        "roles": {
            "host": [START, "godmode", "wait-session 2 420", "wait 60", f"hit {ENEMY} 30",
                     "wait 40", "census 40", "expect-session 2"],
            "guest": [START, f"ensure-map {CATHEDRAL_WARD} {CATHEDRAL_WARD_LAMP}", "godmode",
                      "wait-session 2 420", "wait 20", f"travel {CENTRAL_YHARNAM_LAMP}",
                      "wait-members 2 240", "wait 10", f"hit {ENEMY} 30", "wait 30",
                      "census 40", "expect-session 2"],
        },
        "same_hp": ENEMY,
        "hp_sums_hits": ENEMY,
        "expect": {"host": [OWNER_RUNS],
                   "guest": [r"enemy owner: this game runs map 0x18000000's enemies",
                             OWNER_FOLLOWS]},
        "forbid": {"host": SESSION_HOLDS, "guest": SESSION_HOLDS},
        "timeout": 900,
    },
    "hp-across-blocks": {
        "about": "the guest joins in place in Cathedral Ward while the host is in Central "
                 "Yharnam (two blocks of one area); HP reports still count between them",
        "roles": {
            "host": [START, "godmode", "wait-session 2 420", "wait 30", f"hurt {ENEMY} 200",
                     "wait 30", "snapshot", "expect-session 2"],
            "guest": [START, f"ensure-map {CATHEDRAL_WARD} {CATHEDRAL_WARD_LAMP}", "godmode",
                      "wait-session 2 420", "wait 20", f"travel {CENTRAL_YHARNAM_LAMP}",
                      "wait-members 2 240", "wait 20", "snapshot", f"expect-hp {ENEMY} <= 200",
                      "expect-session 2"],
        },
        "expect": {"guest": [HP_LOWERED]},
        "forbid": {"host": SESSION_HOLDS, "guest": SESSION_HOLDS},
        "timeout": 300,
    },
    "guest-death-seen": {
        "about": "the guest dies (goes down and gives up) away from the lamp in the host's world "
                 "and gets up at the lamp; the host sees it there alive (friend run 2026-10-09: "
                 "invisible after respawning)",
        "settings": PARTY_DEATH,
        "roles": {
            "host": [START, "godmode", "wait-session 2 300", "wait-member-vital down 120",
                     "wait 60", "wait-members 2 120", "wait 10", "snapshot",
                     "expect-others-near 1 12", "expect-session 2"],
            "guest": [START, "wait-session 2 300", "wait 10", f"teleport {AWAY_FROM_LAMP}",
                      "wait 5", *GIVE_UP, "wait 50", "wait-members 2 120", "snapshot",
                      "expect-others-near 1 12", "expect-session 2"],
        },
        "expect": {"guest": [r"party guest died; revived"]},
        "forbid": {"host": SESSION_HOLDS, "guest": SESSION_HOLDS},
        "timeout": 600,
    },
    "echoes-after-crash": {
        "about": "after the guest's crash and rejoin, a kill it makes alone in Cathedral Ward still "
                 "sends the host its echoes and reaches the kill board",
        "roles": {
            "host": [START, "godmode", "wait-session 2 300", "wait-solo 120",
                     "wait-session 2 420", "wait 90", "snapshot", "expect-session 2"],
            "guest": [[START, "godmode", "wait-session 2 300", "wait 15", "done"],
                      [START, "godmode", "wait-session 2 420", "wait 10",
                       f"travel {CATHEDRAL_WARD_LAMP}", "wait 10", *attack(WARD_ENEMY),
                       "wait 15", "snapshot", "expect-session 2"]],
        },
        "expect": {
            "guest": [r"kill board gets 1 kills"],
            "host": [r"added \d+ echoes from a kill another member made apart"],
        },
        "allow": CRASH_CLEANUP,
        "timeout": 1080,
    },
    "lamp-after-crash": {
        "about": "a lamp the guest lights in the session stays lit in both worlds after the "
                 "guest's game crashes and comes back",
        "roles": {
            "host": [START, f"expect-flag {FORBIDDEN_WOODS_LAMP_LIT} 0", "wait-session 2 300",
                     f"wait-flag {FORBIDDEN_WOODS_LAMP_LIT} 1 90", "wait-solo 120",
                     "wait-session 2 420", "wait 20", "snapshot",
                     f"expect-flag {FORBIDDEN_WOODS_LAMP_LIT} 1", "expect-session 2"],
            "guest": [[START, "wait-session 2 300", "wait 30",
                       f"set-flag {FORBIDDEN_WOODS_LAMP_LIT} 1", "wait 20", "done"],
                      [START, "wait-session 2 420", "wait 20", "snapshot",
                       f"expect-flag {FORBIDDEN_WOODS_LAMP_LIT} 1", "expect-session 2"]],
        },
        "allow": CRASH_CLEANUP,
        "timeout": 1080,
    },
    "crash3": {
        "about": "a party of three: one guest's game crashes; host and the other guest stay "
                 "together, and the party is three again once it is back",
        "roles": {
            "host": [START, "godmode", "wait-session 3 480", "wait 30", "wait-session 2 120",
                     "wait-session 3 480", "wait 20", "snapshot", "expect-session 3"],
            "guest": [START, "godmode", "wait-session 3 480", "wait 30", "wait-session 2 120",
                      "snapshot", "expect-session 2", "wait-session 3 480", "wait 20",
                      "snapshot", "expect-session 3"],
            "guest2": [[START, "godmode", "wait-session 3 480", "wait 20", "done"],
                       [START, "godmode", "wait-session 3 480", "wait 20", "snapshot",
                        "expect-session 3"]],
        },
        "allow": CRASH_CLEANUP,
        "timeout": 1500,
    },
    "host-death-boss": {
        "about": "the host dies (goes down and gives up) in the middle of the Cleric Beast fight; "
                 "it gets up without a reload, the session holds and the boss is still there",
        "settings": PARTY_DEATH,
        "roles": {
            "host": [START, "wait-session 2 300", "wait 15", f"hurt {CLERIC_BEAST} 1500",
                     "wait 5", *GIVE_UP, "wait 40", "snapshot", f"expect-hp {CLERIC_BEAST} > 0",
                     f"expect-flag {CLERIC_BEAST_DEFEATED} 0", "expect-session 2"],
            "guest": [START, "godmode", "wait-session 2 300", "wait-member-vital down 120",
                      "wait 60", "snapshot", "expect-session 2"],
        },
        "expect": {"host": [r"party death: giving up ends down \d+; dies \(reset=false",
                            r"live player died in a party session"]},
        "forbid": {"host": SESSION_HOLDS, "guest": SESSION_HOLDS},
        "timeout": 600,
    },
    "down-revive": {
        "about": "party death: the guest falls with 1 HP and goes down instead of dying; the host "
                 "comes over and strikes it until its weapon has dealt the guest's max HP, and it gets up with half its HP",
        "settings": PARTY_DEATH,
        "roles": {
            "host": [START, "godmode", "wait-session 2 300", "wait-member-vital down 120",
                     "shot sees-down", "approach-downed", "wait 2", "shot revive-prompt",
                     *REVIVE_HITS, "wait-member-vital up 30", "wait 3", "shot after-revive",
                     "wait 2", "snapshot", "expect-session 2"],
            "guest": [START, "wait-session 2 300", "wait 15", *LETHAL_FALL, "wait-vital down 30",
                      "expect-hp self == 1", "wait 2", "shot down", "wait-vital up 90",
                      "wait 3", "shot after-revive", "snapshot", "expect-hp self > 1",
                      "expect-session 2"],
        },
        "expect": {
            "host": [r"party death: ((real )?revive hit on|revive swing at)"],
            "guest": [r"party death: down \d+ on map 0x18010000",
                      r"party death: revived from down"],
        },
        "forbid": {"host": SESSION_HOLDS, "guest": SESSION_HOLDS + [r"party guest died"]},
        "timeout": 300,
    },
    "down-wait": {
        "about": "party death: the guest goes down (no kneel, Kaniel 2026-10-15) while nobody "
                 "strikes it for 12 s "
                 "(playtest 2026-10-13: the others saw Lisara kneel and no strike landed, so he had "
                 "to give up); then the host strikes it and it gets up",
        "settings": PARTY_DEATH,
        "roles": {
            "host": [START, "godmode", "wait-session 2 300", "wait-member-vital down 120",
                     "wait 12", "shot sees-down", "approach-downed", "wait 2", *REVIVE_HITS,
                     "wait-member-vital up 30", "wait 3", "shot after-revive", "wait 2", "snapshot",
                     "expect-session 2"],
            "guest": [START, "wait-session 2 300", "wait 15", *LETHAL_FALL, "wait-vital down 30",
                      "wait 2", "shot down", "wait-vital up 120", "wait 3", "snapshot",
                      "expect-hp self > 1", "expect-session 2"],
        },
        "expect": {
            "host": [r"party death: (real revive hit on|revive swing at)"],
            "guest": [r"party death: down \d+ on map 0x18010000",
                      r"party death: revived from down"],
        },
        "forbid": {"host": SESSION_HOLDS + [KNEEL],
                   "guest": SESSION_HOLDS + [r"party guest died", KNEEL]},
        "timeout": 300,
    },
    "portrait": {
        "about": "party portraits: each player opens its status page (its face is taken from it), "
                 "then they join; each party list shows the other's face with its icon as a badge (the "
                 "downed drop once the guest falls)",
        "settings": PARTY_DEATH,
        "roles": {
            "host": [START, *OPEN_STATUS, "shot status", *CLOSE_MENU, "godmode",
                     "wait-session 2 300", "wait 15", "shot party", "wait-member-vital down 120",
                     "wait 2", "shot sees-down", "snapshot", "expect-session 2"],
            "guest": [START, *OPEN_STATUS, "shot status", *CLOSE_MENU, "wait-session 2 300",
                      "wait 15", "shot party", *LETHAL_FALL, "wait-vital down 30", "wait 2",
                      "shot down", "snapshot", "expect-session 2"],
        },
        "expect": {
            "host": [r"party portraits: took the character's portrait from the status page",
                     r"party portraits: 7269675f6775657374's portrait came",  # rig_guest
                     r"Party portraits: slot 0 texture 0x[0-9a-f]+ .* shows the member's picture"],
            "guest": [r"party portraits: took the character's portrait from the status page",
                      r"party portraits: 7269675f686f7374's portrait came",  # rig_host
                      r"Party portraits: slot 0 texture 0x[0-9a-f]+ .* shows the member's picture"],
        },
        "forbid": {"host": SESSION_HOLDS, "guest": SESSION_HOLDS},
        "timeout": 300,
    },
    "portrait-auto": {
        "about": "each player's portrait without the status page (Kaniel 2026-10-13: 'ideally "
                 "yes'): no kept portraits, the players never open a menu; each game draws its "
                 "character's status model off-screen and the other's party list shows it",
        "no_portraits": True,
        "roles": {
            "host": [START, "wait-session 2 300", "wait 20", "shot party", "snapshot",
                     "expect-session 2"],
            "guest": [START, "wait-session 2 300", "wait 20", "shot party", "snapshot",
                      "expect-session 2"],
        },
        "expect": {role: [r"party portraits: off-screen model drawn; reading it back",
                          r"party portraits: took the character's portrait",
                          r"Party portraits: slot 0 texture 0x[0-9a-f]+ .* shows the member's "
                          r"picture"]
                   for role in ("host", "guest")},
        "forbid": {"host": SESSION_HOLDS, "guest": SESSION_HOLDS},
        "timeout": 300,
    },
    "portrait-dressed": {
        "about": "party portraits on a dressed character (Kaniel 2026-10-13: are clothes on the portrait?): "
                 "Kaniel's guest character, in the starting Black Hood and Foreign Garb, opens its "
                 "status page (Square shows its face and model) and its portrait is taken from the "
                 "model: head and shoulders, hood and collar",
        "saves": "kaniel-1011-guest",
        "roles": {"host": [START, "shot world-before", *OPEN_STATUS, "press square 200", "wait 3",
                           "shot status-model", *CLOSE_MENU, "wait 5", "shot world"]},
        "expect": {"host": [r"party portraits: took the character's portrait from the status page"]},
        "timeout": 300,
    },
    "give-up": {
        "about": "party death: the downed guest holds Circle and dies as a party member dies "
                 "today: it gets up at its lamp without a reload and the session holds",
        "settings": PARTY_DEATH,
        "roles": {
            "host": [START, "godmode", "wait-session 2 300", "wait-member-vital down 120",
                     "wait 60", "snapshot", "expect-session 2"],
            "guest": [START, "wait-session 2 300", "wait 15", *LETHAL_FALL, "wait-vital down 30",
                      "wait 2", "press circle 3600", "wait-vital dead 15", "wait-vital up 90",
                      "wait 10", "snapshot", "expect-session 2"],
        },
        "expect": {
            "guest": [r"party death: giving up ends down \d+; dies \(reset=false",
                      r"party guest died; revived"],
        },
        "forbid": {"host": SESSION_HOLDS, "guest": SESSION_HOLDS + [r"reset=true"]},
        "timeout": 600,
    },
    "wipe": {
        "about": "party death: the guest goes down, then the host falls with nobody left "
                 "standing; both die (the guest by the server's wipe) and wake at their lamps "
                 "with the map reloaded, and the session holds",
        "settings": PARTY_DEATH,
        "roles": {
            "host": [START, "wait-session 2 300", "wait-member-vital down 120", "wait 3",
                     *LETHAL_FALL, "wait-vital dead 30", "wait-vital up 120",
                     "wait-members 2 240", "wait 15", "snapshot", "expect-session 2"],
            "guest": [START, "wait-session 2 300", "wait 15", *LETHAL_FALL, "wait-vital down 30",
                      "wait-vital dead 60", "wait-vital up 120", "wait-members 2 240", "wait 15",
                      "snapshot", "expect-session 2"],
        },
        "expect": {
            "host": [r"party death: died for real on map 0x18010000 \(reset=true\)",
                     r"travelling there \(reset=true\)", BLOODSTAIN_DROPPED],
            "guest": [r"party death: wipe \d+ ends down \d+; dies \(reset=true",
                      r"travelling there \(reset=true\)", BLOODSTAIN_DROPPED],
        },
        "forbid": {"host": SESSION_HOLDS + [BLOODSTAIN_STOLEN],
                   "guest": SESSION_HOLDS + [BLOODSTAIN_STOLEN]},
        "timeout": 300,
    },
    "down-alone": {
        "about": "party death: with the host away in the Hunter's Dream nobody else stands on the "
                 "guest's map, so its lethal fall is a real death that reloads the map (a wipe "
                 "of one)",
        "settings": PARTY_DEATH,
        "roles": {
            "host": [START, "godmode", "wait-session 2 300", "wait 10", f"travel {HUNTERS_DREAM}",
                     "wait 120", f"travel {CENTRAL_YHARNAM_LAMP}", "wait-members 2 240",
                     "wait 10", "snapshot", "expect-session 2"],
            "guest": [START, "wait-session 2 300", "wait 70", *LETHAL_FALL, "wait-vital dead 30",
                      "wait-vital up 120", "wait-members 2 300", "wait 10", "snapshot",
                      "expect-session 2"],
        },
        "expect": {
            "guest": [r"party death: died for real on map 0x18010000 \(reset=true\)",
                      r"travelling there \(reset=true\)"],
        },
        "forbid": {"host": SESSION_HOLDS, "guest": SESSION_HOLDS + [r"party death: down \d+"]},
        "timeout": 900,
    },
    "down-by-enemy": {
        "about": "party death: the guest with 1 HP stands next to an ordinary enemy until it "
                 "hits; it goes down, the enemy leaves it alone (still 1 HP after 20 s), and the "
                 "host revives it",
        "settings": PARTY_DEATH,
        "roles": {
            "host": [START, "godmode", "wait-session 2 300", "wait-member-vital down 150",
                     "wait 22", "approach-downed", "wait 2", *REVIVE_HITS,
                     "wait 5", "snapshot", "expect-session 2"],
            "guest": [START, "wait-session 2 300", "wait 15", "hurt self 1", f"approach {ENEMY}",
                      "wait-vital down 90", "wait 20", "expect-hp self == 1", "wait-vital up 90",
                      "wait 3", "snapshot", "expect-hp self > 1", "expect-session 2"],
        },
        "expect": {
            "host": [r"party death: ((real )?revive hit on|revive swing at)"],
            "guest": [r"party death: down \d+ on map 0x18010000",
                      r"party death: enemies forgot \d+ target\(s\) on downed players "
                      r"\(self down=true",
                      r"party death: revived from down"],
        },
        "forbid": {"host": SESSION_HOLDS,
                   "guest": SESSION_HOLDS + [r"party death: killed while down"]},
        "timeout": 300,
    },
    "down-revive3": {
        "about": "party death in a party of three: the guest goes down and the second guest, "
                 "not the host, revives it",
        "settings": PARTY_DEATH,
        "roles": {
            # The host only watches: with the downed body's swings counted the revive can be over
            # in seconds, before its board shows the down (rig 2026-10-14).
            "host": [START, "godmode", "wait-session 3 480", "wait 60", "snapshot",
                     "expect-session 3"],
            # Checked at once: a missed swing counts within 0.4 s now (Kaniel 2026-10-16), so the
            # second guest's first ones may already be in a few seconds later.
            "guest": [START, "wait-session 3 480", "wait 20", *LETHAL_FALL, "wait-vital down 30",
                      "expect-hp self == 1", "wait 2", "shot down", "wait-vital up 90",
                      "wait 3", "shot after-revive", "snapshot", "expect-hp self > 1",
                      "expect-session 3"],
            "guest2": [START, "godmode", "wait-session 3 480", "wait-member-vital down 180",
                       "shot sees-down", "approach-downed", "wait 2", "shot revive-prompt",
                       *REVIVE_HITS, "wait 5", "snapshot", "expect-session 3"],
        },
        "expect": {
            "guest": [r"party death: down \d+ on map 0x18010000",
                      r"party death: revived from down"],
            "guest2": [r"party death: ((real )?revive hit on|revive swing at)"],
        },
        "forbid": {"host": SESSION_HOLDS + [r"party death: ((real )?revive hit on|revive swing at)"],
                   "guest": SESSION_HOLDS, "guest2": SESSION_HOLDS},
        "timeout": 900,
    },
    "wipe3": {
        "about": "party death in a party of three: both guests go down, then the host falls with "
                 "nobody left standing; all three die and wake at their lamps with the map "
                 "reloaded, and the session holds",
        "settings": PARTY_DEATH,
        "roles": {
            "host": [START, "wait-session 3 480", "wait-member-vital down 180", "wait 25",
                     "shot sees-both-down", f"teleport {WIPE3_SPOT['host']}", "wait 2",
                     *LETHAL_FALL, "wait-vital dead 30", "wait-vital up 150",
                     "wait-members 3 300", "wait 15", "snapshot", "expect-session 3"],
            "guest": [START, "wait-session 3 480", "wait 20", f"teleport {WIPE3_SPOT['guest']}",
                      "wait 2", *LETHAL_FALL, "wait-vital down 30", "wait-vital dead 90",
                      "wait-vital up 150", "wait-members 3 300", "wait 15", "snapshot",
                      "expect-session 3"],
            "guest2": [START, "wait-session 3 480", "wait-member-vital down 180", "wait 5",
                       f"teleport {WIPE3_SPOT['guest2']}", "wait 2", *LETHAL_FALL,
                       "wait-vital down 30", "wait-vital dead 90",
                       "wait-vital up 150", "wait-members 3 300", "wait 15", "snapshot",
                       "expect-session 3"],
        },
        "expect": {
            "host": [r"party death: died for real on map 0x18010000 \(reset=true\)",
                     r"travelling there \(reset=true\)", BLOODSTAIN_DROPPED],
            "guest": [r"party death: wipe \d+ ends down \d+; dies \(reset=true",
                      r"travelling there \(reset=true\)", BLOODSTAIN_DROPPED],
            "guest2": [r"party death: down \d+ on map 0x18010000",
                       r"party death: wipe \d+ ends down \d+; dies \(reset=true",
                       r"travelling there \(reset=true\)", BLOODSTAIN_DROPPED],
        },
        "forbid": {"host": SESSION_HOLDS + [BLOODSTAIN_STOLEN],
                   "guest": SESSION_HOLDS + [BLOODSTAIN_STOLEN],
                   "guest2": SESSION_HOLDS + [BLOODSTAIN_STOLEN]},
        "timeout": 300,
    },
    # Rig refresh (2026-10-15): its own boot, a brand-new character's first load in a team (the
    # opening cutscene). 0.2.3: the cutscene ran under the team's load screen and could not be
    # skipped; the title's Cross skips it now and the load screen goes once the world is in.
    "new-char-cutscene": {
        "about": "both games on brand-new characters (rig/saves/new-character) load into their "
                 "team: the opening cutscene is skipped, the team's load screen goes away, they "
                 "are in one session",
        "saves": "new-character",
        "roles": {role: ["wait-session 2 240", "wait 25", "expect-session 2"]
                  for role in ("host", "guest")},
        "expect": {role: [r"load view is gone"] for role in ("host", "guest")},
        "forbid": {role: SESSION_HOLDS + [r"load view is up \(\d+ rows\)(?![\s\S]*load view "
                                          r"is gone)"] for role in ("host", "guest")},
        "timeout": 300,
    },
    # Kaniel 2026-10-16: the port's settings menu (Insert) in the style of the team screens and
    # the party list: the game's menu art and font. A picture of each page; an effect's row is
    # switched and back, and the menu saves the change when it closes; the menu in Russian (the
    # serif's letters behind the game's font); the quit box, cancelled.
    "settings-menu": {
        "about": "the port's settings menu (Insert) drawn with the game's menu art and font: a "
                 "picture of every page, a row's value changed and back, saved on close, the "
                 "menu in Russian, the quit box cancelled",
        "roles": {
            "host": [START, "wait 5", "keys toggle", "wait 3", "shot menu-display",
                     *[step for name in ("upscaler", "effects", "controls", "advanced", "save")
                       for step in ("keys r1", "wait 2", f"shot menu-{name}")],
                     "keys r1 r1 r1 down", "wait 2", "shot menu-effect-row",
                     "keys right", "wait 2", "shot menu-effect-changed",
                     "keys left", "wait 2",
                     # The Display page's language row: five down from its first.
                     "keys l1 l1 down down down down down", "wait 2", "keys right", "wait 2",
                     "shot menu-russian", "keys left", "wait 2",
                     # Up from a page's first row is Quit (nothing waits for a restart): its box
                     # opens on Cancel, which Cross takes.
                     "keys l1 r1 up enter", "wait 2", "shot menu-quit", "keys enter", "wait 3",
                     "shot menu-closed"],
        },
        "expect": {"host": [r"menu keys right"]},
        "console_expect": {"host": [r"Overlay: the settings menu uses \d+ of \d+ pieces of the "
                                    r"game's menu art",
                                    r"Overlay: settings changed in the menu, saved"]},
        "timeout": 300,
    },
    "soak": {
        "about": "half an hour of trips back and forth with the session up (leaks, slow growth, "
                 "a trip that breaks after many)",
        "roles": {
            "host": [START, "godmode", "wait-session 2 300",
                     *[step for _ in range(12) for step in
                       (f"travel {HUNTERS_DREAM}", "wait 20", f"travel {CENTRAL_YHARNAM_LAMP}",
                        "wait-members 2 240", "wait 40")],
                     "snapshot", "expect-session 2"],
            "guest": [START, "godmode", "wait-session 2 300", "wait 2100", "wait-members 2 240",
                      "snapshot", "expect-session 2"],
        },
        "forbid": {"host": SESSION_HOLDS, "guest": SESSION_HOLDS},
        "timeout": 2700,
    },
}

# On Kaniel's characters the host's story switched off the copy of Iosefka by her door: so does
# the guest.
SCENARIOS["iosefka-mine"]["roles"] = {
    role: [*steps[:6], f"expect-disabled {CLINIC} 1 30", *steps[6:]]
    for role, steps in SCENARIOS["iosefka-new"]["roles"].items()
}


# Chalice dungeons. The Dream's first ritual altar: talk t210060 on the talk dummy c9020_0005 at
# (-18.27, -5.43, -16.88) in m21_00's MSB, used from 1 m east of it facing west. Its talk sets
# 6708 on the first use (and shows a message first); its menu is Chalice Ritual (first),
# Chalice Dungeon Search, Search by Chalice Glyph; Chalice Ritual, then Conduct Ritual, asks
# "Conduct a ritual...?" (No first: left is Yes). After the ritual the altar's menu has its
# layers, Layer One first (the warp 72100300). Chalice dungeons are map 0x1d000000.
# A headless game draws no menus: BB_GAME_MENU_TRACE_IDS=262000-267999 makes the port print
# which of the altar's texts it shows ("Game menu: shows ...").
PTHUMERU_CHALICE = "goods:6109"
RITUAL_BLOOD_1 = "goods:7000"
ALTAR_SPOT = "-17.27 -5.23 -16.88 1.571"
CHALICE_DUNGEON = "0x1d000000"


def CHALICE_ENTER(role: str) -> list[str]:
    """The Pthumeru Chalice and its materials, the Dream, the altar (its first use's message),
    Chalice Ritual, Conduct Ritual, Yes, then the altar again and Layer One."""
    return [f"trade-stock {PTHUMERU_CHALICE} 1", f"trade-stock {RITUAL_BLOOD_1} 10",
            "trade-stock echoes 20000", f"travel {HUNTERS_DREAM}", "wait 5",
            f"teleport {ALTAR_SPOT}", "wait 2", "press cross 200", "wait 7",
            *[step for _ in range(4) for step in ("press cross 200", "wait 7")],
            "press left 200", "wait 1", "press cross 200", "wait 20", f"log {role} ritual done",
            *[step for _ in range(3) for step in ("press cross 200", "wait 10")],
            f"wait-map {CHALICE_DUNGEON} 180", "wait 10", "snapshot",
            f"log {role} in the dungeon"]


SCENARIOS["chalice-ritual"] = {
    "about": "a ritual with the Pthumeru Chalice at the Dream's altar, then its Layer One (the "
             "server's own dungeon for the chalice: none made the game return to the title)",
    "roles": {"host": [START, "godmode", *CHALICE_ENTER("host"), f"expect-map {CHALICE_DUNGEON}"]},
    "expect": {"host": [r"host in the dungeon"]},
    "timeout": 300,
}

# Each member of a team of two conducts the same chalice's ritual at its own altar; the server
# gives both the one dungeon it keeps for that chalice, and each enters Layer One alone: they
# meet in it.
SCENARIOS["chalice-team"] = {
    "about": "a team of two enters the same chalice dungeon, each from its own altar",
    "roles": {
        "host": ["team-create", "team-invite rig_guest", "team-wait 2", "team-start", START,
                 "godmode", "wait-session 2 300", "wait 10", *CHALICE_ENTER("host"),
                 "wait-members 2 300", "wait 20", "expect-session 2", "snapshot"],
        "guest": ["team-accept", "team-ready", START, "godmode", "wait-session 2 300", "wait 30",
                  "wait 10",
                  *CHALICE_ENTER("guest"), "wait-members 2 300", "wait 20", "expect-session 2",
                  "snapshot"],
    },
    "expect": {"host": [r"host in the dungeon"], "guest": [r"guest in the dungeon"]},
    "forbid": {"host": SESSION_HOLDS, "guest": SESSION_HOLDS},
    "allow": CRASH_CLEANUP,
    "timeout": 1200,
}
