# A hosted server

Today a host player runs shadNet at home and friends connect over Radmin. A hosted server is one
shadNet on the internet that strangers use: every team's world on one machine, kept apart. This
page is what the `hosted-server` branch adds for that, what the load tests showed, and what is
still missing before a public launch.

## `Hosted=true`

Off by default: a host's own server behaves as before. On (`shadnet.cfg`):

| What | Where | Why |
| --- | --- | --- |
| Passwords checked on a thread pool, not the link's thread; a wrong one is not hashed again for 30 s; at most 512 checks queued | `link_server.cpp` `SignIn` | each new id cost ~3 password hashes (100k rounds) on the thread that relays every game: 30 new players froze pings for 1.7 s |
| Hellos an address (4/s, burst 20), games an address (8), new accounts an address an hour (5) | `admission.h` | bots making accounts; one PC flooding hellos |
| Each link's datagrams (1500/s) and bytes (400 KB/s); past them dropped, 5 seconds past them in a minute bans the address for 15 min | `admission.h` | a game sends ~60-200 datagrams and ~10 KB a second |
| `banned.txt`: one address or player id a line, read every 30 s | `link_server.cpp` | the operator's bans |
| `HostedBuild`: the build every game must say | `server_link.cpp` | otherwise the first game to link decides it |
| Lobby and party names must be the player's own id (hex, as the games say them) | `bloodborne_world.cpp` `OwnName` | anyone could act in the lobby, or say hello into a party, as another player |
| A party hello must be for the world the player's team plays | `bloodborne_world.cpp` `OnHello` | any player could join any world by its key |
| A world is its players' (`TeamWorldInfo.members`): listed to them only, forgotten only by them (each for itself) | `team_lobby.cpp` | every world was listed to everyone; anyone could forget anyone's; 16 worlds server-wide |
| The lobby goes out at most every 250 ms, only to players whose own state changed, listing their team, invites and 32 others | `team_lobby.cpp` `StateFor`, `bloodborne_world.cpp` `SendLobby` | every lobby change sent every player the whole server's list: O(n^2), 216 Mbit/s at 120 players |

Two fixes are on for everyone: a TCP packet size under its header no longer loops the session for
ever (`client_session.cpp`), and a datagram from one game no longer walks every member's stream
(`Service::FlushSome`; that walk was the limit past ~600 games).

Settings: `Hosted`, `HostedBuild`, `HostedBanList`, `HostedLinksPerAddress`,
`HostedHellosPerSecond`, `HostedNewAccountsPerHour`, `HostedDatagramsPerSecond`,
`HostedBytesPerSecond`, `HostedLobbyPlayers`, `HostedLobbyIntervalMs`, `HostedWorldsPerPlayer`.

Invasions (a solo player invading a team world whose leader allows it) are off unless
`Invasions=true`, hosted or at home; the server side, its settings and what the games still need:
[INVASIONS.md](INVASIONS.md).

The games need no change for any of this (the emulator already says its id in hex as its names;
check the port does too). One game-side change is wanted: on a hosted server the lobby lists 32
players, so inviting a friend needs invite by name or code.

## Load test

`server/loadtest/`: `bbload` runs simulated games that speak the real link from their own UDP
ports and loopback addresses: hello and sign-in, ping, the team lobby (create, invite, accept,
ready, start), the party hello, vitals twice a second, kill proposals, the map every second, and
game packets to each teammate at the rates the rig measured (enemy owner ~87 a second, follower
~8). Every packet carries its send time and number, so the receiver measures the relay's delay and
losses. `run_load.py` starts a fresh shadNet, runs the players over several bbload processes,
samples the server's CPU (and its link thread's) and memory, and can run `attack.py` beside them.

```
cmake --build build --target shadnet bbload
python3 server/loadtest/run_load.py --shadnet build/shadnet --bbload build/bbload \
    --players 300 --team-size 3 --seconds 60 --cfg Hosted=true \
    [--attack "signin-flood --rate 3000 --seconds 30 --addresses 500"]
```

Results (2026-10-23; one 4-core cloud container running the server and the players together, so
the players' processes take cores from the server; teams of 3):

| Players | Server | In game | Relay p50 / p99 | Lost | Link thread | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| 30 | today | 30/30 | 0.2 / 5.7 ms | <1% | ~2% | pings stall 1.7 s while players sign in |
| 60 | today | 54/60 | 0.3 / 10 ms | 2% | ~5% | joins up to 12.5 s |
| 120 | today | 50/120 | 41 / 1577 ms | 65% | 100% | lobby updates: 216 Mbit/s out |
| 120 | hosted | 120/120 | 0.3 / 1.7 ms | 0.1% | 8% | |
| 300 | hosted | 300/300 | 0.4 / 7 ms | 0.4% | 21% | 18 Mbit/s out |
| 1000 | hosted | 1002/1002 | 0.5-0.8 / 10-18 ms | 0 | 65-70% | steady state; ~50 Mbit/s out; 38 MB of memory |

At 1000 the join rush (1000 new accounts within 10 s) takes up to 30-35 s; players arriving over
minutes would not see it. "Lost" counts gaps in each sender's numbers on one map; packets the
server keeps from a player on another map (by design) are not losses. Without the FlushSome fix 1000 players saturated the
link thread (relay p99 9 s).

Attacks beside 300 players (hosted):

| Attack | What happened | Players |
| --- | --- | --- |
| 3000 hellos/s with new ids from one address, 30 s | 5 accounts made, the rest dropped | no change |
| the same from 500 addresses (a botnet) | 1181 accounts made; the sign-in threads busy (server 268% CPU) | no change (link thread 19%) |
| one signed-in game sending 30,000 datagrams/s | banned within ~5 s | no change |

## How far one machine goes

The link thread (relay, worlds, lobby) is the limit: ~65% of one core at 1000 players here. A
dedicated 4-8 core machine should hold ~1000-1500 players in one process (a guess past what was
measured). Past that, worlds go to several shadNet processes (each a share of the worlds, its own
port) with a small directory saying which process runs which world: that is where Redis (or a
Postgres table) earns its place, not before. Each world's log has one writer, its process, so no
distributed database is needed for "sync".

Storage: SQLite (accounts) and the world folders (journal + snapshots) are right for one machine.
Postgres helps only with several processes or machines; moving to it is ~3-4 weeks (thread-per-
session connections, SQLite-only SQL). Backups: `server/hosting/backup.sh` (journal before
world.pb, `sqlite3 .backup`, restic to S3/B2/Storage Box), with a restore drill in
`server/hosting/README.md`.

## Before a public launch

Server only:
- The HTTP ports (31315 summon broker, 20443 backend) take the player id from the request: only
  answer addresses with a signed-in link, as that link's player.
- Caps on the reliable streams' reassembly and out-of-order buffers and on the protobuf strings.
- Store each account's virtual address and refuse a colliding new account (24-bit hash).
- World journal writes off the link thread (each record is fsynced on it now).
- Make the account limit hold against botnets: a proof-of-work in the hello, or sign-up through
  the launcher with a server-signed token.
- Retire worlds nobody played for months (disk).

Game and launcher:
- A cookie handshake and an HMAC on every datagram (secrets cross the internet in clear today, and
  after the hello a datagram is trusted by its address), ~3-5 days; or Noise, ~1.5-2 weeks.
- Invite by name or code (the lobby lists 32 players).
- A fresh id when the server refuses one (virtual address collision).
