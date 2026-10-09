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
teleport <x> <y> <z>, press <button> [<ms>], stick <x> <y> [<ms>], snapshot, log <text>,
expect-hp <handle|self> <op> <value>, expect-session <n>, expect-map <map>,
set-flag <flag> <0|1>, expect-flag <flag> <0|1>, wait-flag <flag> <0|1> [<timeout>],
godmode [off] (the player's HP kept at max), done. Targets: nearest, self or a handle in hex; kill
uses the game's own kill (as an event's ForceCharacterDeath), so bosses die in one step.
"""

HUNTERS_DREAM = 2102962
CENTRAL_YHARNAM = "0x18010000"
CENTRAL_YHARNAM_LAMP = 2412950  # the first lamp, where the rig's characters start
CATHEDRAL_WARD = "0x18000000"
CATHEDRAL_WARD_LAMP = 2402950
ENEMY = "0x1001400b"  # 484 HP, 17 m from the lamp (farther ones are not awake)
CLERIC_BEAST = "0x1001409f"  # entity 2410800; its death runs event 12411700
CLERIC_BEAST_DEFEATED = 2411  # set by event 12411700 after Handle Boss Defeat
FORBIDDEN_WOODS_LAMP_LIT = 12707810  # a lamp neither rig character has lit (see LampOfActiveFlag)
START = f"ensure-map {CENTRAL_YHARNAM} {CENTRAL_YHARNAM_LAMP}"
# The session must hold: a game that leaves the room (LeaveRoom) broke it.
SESSION_HOLDS = [r"sceNpMatching2LeaveRoom: called"]

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
        "about": "host and guest join each other by themselves (the bells ring on their own)",
        "roles": {
            "host": [START, "wait-session 2 300", "wait 10", "snapshot", "expect-session 2"],
            "guest": [START, "wait-session 2 300", "wait 10", "snapshot", "expect-session 2"],
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
            "host": [START, "godmode", "wait-session 2 300", "wait 45", f"kill {ENEMY}", "wait 5",
                     "wait-members 2 240", "wait 20", "snapshot"],
            "guest": [START, "godmode", "wait-session 2 300", "wait 10", f"travel {HUNTERS_DREAM}",
                      "wait 40", f"travel {CENTRAL_YHARNAM_LAMP}", "wait-members 2 240", "wait 25",
                      "snapshot"],
        },
        "expect": {
            "host": [r"kill board gets 1 kills"],
            "guest": [r"hid \d+ enemies a member killed on map 0x18010000"],
        },
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
                      f"travel {CATHEDRAL_WARD_LAMP}", "wait 10", "snapshot", "kill nearest",
                      "wait 15", "snapshot"],
        },
        "expect": {"host": [r"added \d+ echoes from a kill another member made apart"]},
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
    "join3": {
        "about": "a party of three: the host rings again after the first guest and both guests "
                 "join",
        "roles": {
            "host": [START, "godmode", "wait-session 3 480", "wait 10", "snapshot",
                     "expect-session 3"],
            "guest": [START, "godmode", "wait-session 3 480", "wait 10", "snapshot",
                      "expect-session 3"],
            "guest2": [START, "godmode", "wait-session 3 480", "wait 10", "snapshot",
                       "expect-session 3"],
        },
        "expect": {"host": [r"auto-rejoin started search effect=9000(.|\n)*"
                            r"auto-rejoin started search effect=9000"]},
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
