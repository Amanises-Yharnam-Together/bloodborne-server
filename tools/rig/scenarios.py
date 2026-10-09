# SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later
"""The rig's test scenarios (run_scenario.py): each role's autoplay script, the lines its log
must show ("expect"), the lines it must not show ("forbid") and the known problems it may show
("allow").

Every run starts from the baseline saves (run_scenario.py --save-baseline): every game at the first
Central Yharnam lamp. Every script starts with START (travel there unless already there).

Commands (see RunAutoplayStep in bloodborne_re.cpp): wait <s>, wait-session <n> [<timeout>],
wait-members <n> [<timeout>] (party members on this map), wait-map <map> [<timeout>],
travel <warp point> [<timeout>], ensure-map <map> <warp point> [<timeout>] (travel unless
there already), hurt <nearest|handle> <hp>, kill <nearest|self|handle>,
teleport <x> <y> <z>, approach <target> [<dx> [<dz>]], mark <target> (later "marked"),
attack <target> [<timeout>] (1 HP left, then light attacks from each side until it dies),
press <button> [<ms>], stick <x> <y> [<ms>], snapshot, log <text>,
expect-hp <handle|self> <op> <value>, expect-session <n>, expect-map <map>,
set-flag <flag> <0|1>, expect-flag <flag> <0|1>, wait-flag <flag> <0|1> [<timeout>],
wait-solo [<timeout>] (until the session is gone), note-max-hp (the player's, for
expect-max-hp <target> <op> <value|noted>), expect-effect <SpEffect> <0|1>, godmode [off] (the player's HP kept at max), done. Targets: nearest, self or a handle in hex; kill
uses the game's own kill (as an event's ForceCharacterDeath), so bosses die in one step.
"""

HUNTERS_DREAM = 2102962
CENTRAL_YHARNAM = "0x18010000"
CENTRAL_YHARNAM_LAMP = 2412950  # the first lamp, where the rig's characters start
CATHEDRAL_WARD = "0x18000000"
CATHEDRAL_WARD_LAMP = 2402950
ENEMY = "0x1001400b"  # 484 HP, 17 m from the lamp (farther ones are not awake)
AWAY_FROM_LAMP = "-215.0 -54.5 178.0"  # beside that enemy, 17 m from the lamp
WARD_ENEMY = "0x10010019"  # 211 HP, 7 m from the Cathedral Ward lamp (the nearest, 0x10010075,
# has 999 HP and dies of nothing: an object, as Central Yharnam's 0x100140a0)
CLERIC_BEAST = "0x1001409f"  # entity 2410800; its death runs event 12411700
CLERIC_BEAST_DEFEATED = 2411  # set by event 12411700 after Handle Boss Defeat
GASCOIGNE = "0x1001409c"  # entity 2410810 (his beast form: 0x1001409e, 2410811)
# Event 12411800's own flag: on, the map's load puts Gascoigne down (Force Character Death);
# off, he waits for the fight. Both rig saves have him beaten.
GASCOIGNE_DEFEATED = 12411800
FORBIDDEN_WOODS_LAMP_LIT = 12707810  # a lamp neither rig character has lit (see LampOfActiveFlag)
START = f"ensure-map {CENTRAL_YHARNAM} {CENTRAL_YHARNAM_LAMP}"
LEVEL_SYNC = 9200  # the SpEffect that scales a guest down to the host's level
# The session must hold: a game that leaves the room (LeaveRoom) broke it.
SESSION_HOLDS = [r"sceNpMatching2LeaveRoom: called"]
# A member's game died: the others' games close the room it was in (and open a new one for its
# return), which check_party_log reports as leaving the session outside a trip.
CRASH_CLEANUP = [r"left the session outside a trip"]


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
        "timeout": 420,
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
        "about": "host and guest join each other by themselves (the bells ring on their own); the "
                 "guest keeps its full HP and its own level",
        "roles": {
            "host": [START, "wait-session 2 300", "wait 10", "snapshot", "expect-session 2"],
            "guest": [START, "note-max-hp", "wait-session 2 300", "wait 10", "snapshot",
                      "expect-session 2", "expect-max-hp self == noted",
                      f"expect-effect {LEVEL_SYNC} 0"],
        },
        "expect": {
            "host": [r"auto-rejoin started search effect=9000"],
            "guest": [r"auto-rejoin started search effect=9005"],
        },
        "forbid": {"host": SESSION_HOLDS, "guest": SESSION_HOLDS},
        "timeout": 480,
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
        "timeout": 600,
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
            "guest": [r"hid 1 enemies a member killed on map 0x18010000"],
        },
        "forbid": {"host": only_kill(ENEMY)},
        "timeout": 720,
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
            "host": [r"hid 1 enemies a member killed on map 0x18010000"],
        },
        "forbid": {"guest": only_kill(ENEMY)},
        "timeout": 720,
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
        "expect": {"guest": [r"lowered \d+ enemies to another member's HP"]},
        "timeout": 720,
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
        "forbid": {
            "host": SESSION_HOLDS + [r"hid \d+ enemies a member killed"],
            "guest": SESSION_HOLDS + [r"kill board gets [^\n]*0x100140(9c|9e) "],
        },
        "timeout": 720,
    },
    "lamp-share": {
        "about": "a lamp the guest lights is lit in the host's world too",
        "roles": {
            "host": [START, f"expect-flag {FORBIDDEN_WOODS_LAMP_LIT} 0", "wait-session 2 300",
                     f"wait-flag {FORBIDDEN_WOODS_LAMP_LIT} 1 90", "snapshot"],
            "guest": [START, "wait-session 2 300", "wait 10",
                      f"set-flag {FORBIDDEN_WOODS_LAMP_LIT} 1", "wait 30", "snapshot"],
        },
        "timeout": 600,
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
        "timeout": 720,
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
        "timeout": 780,
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
            "guest": [r"guest alone on map 0x18000000 took over \d+ enemies",
                      r"guest no longer alone; handed back"],
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
        "about": "the host dies; it gets up without a reload and the session holds",
        "roles": {
            "host": [START, "wait-session 2 300", "wait 10", "kill self", "wait 40", "snapshot",
                     "expect-session 2"],
            "guest": [START, "godmode", "wait-session 2 300", "wait 70", "snapshot",
                      "expect-session 2"],
        },
        "expect": {"host": [r"live player died in a party session"]},
        "forbid": {"host": SESSION_HOLDS, "guest": SESSION_HOLDS},
        "timeout": 600,
    },
    "guest-death": {
        "about": "the guest dies in the host's world; it gets up there and the session holds",
        "roles": {
            "host": [START, "godmode", "wait-session 2 300", "wait 70", "snapshot",
                     "expect-session 2"],
            "guest": [START, "wait-session 2 300", "wait 10", "kill self", "wait 40", "snapshot",
                      "expect-session 2"],
        },
        "expect": {"guest": [r"party guest died; revived"]},
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
        "timeout": 900,
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
        "timeout": 900,
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
        "timeout": 900,
    },
    "trip3": {
        "about": "a party of three apart: the second guest waits in the Dream while the host "
                 "goes there and back; the host re-creates only the guest on its map, then the "
                 "second guest when it comes back",
        "roles": {
            "host": [START, "godmode", "wait-session 3 480", "wait 40", f"travel {HUNTERS_DREAM}",
                     "wait 5", f"travel {CENTRAL_YHARNAM_LAMP}", "wait 60", "wait-members 3 300",
                     "wait 20", "snapshot", "expect-session 3"],
            "guest": [START, "godmode", "wait-session 3 480", "wait 150", "wait-members 3 300",
                      "wait 25", "snapshot", "expect-session 3"],
            "guest2": [START, "godmode", "wait-session 3 480", "wait 10",
                       f"travel {HUNTERS_DREAM}", "wait 90", f"travel {CENTRAL_YHARNAM_LAMP}",
                       "wait-members 3 300", "wait 25", "snapshot", "expect-session 3"],
        },
        "expect": {
            "host": [r"re-creating rig_guest on map 0x18010000 \(1 of 2 members\)",
                     r"re-creating rig_guest2 on map 0x18010000"],
        },
        "timeout": 1200,
    },
}
