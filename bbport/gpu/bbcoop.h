/* SPDX-License-Identifier: GPL-2.0-or-later */
/* bbport co-op: the emulator's seamless co-op (../emulator/src/core/bloodborne_*.cpp) in the
 * native port. What the loader and the C runtime call; gpu/bbcoop/ implements it. */
#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* A segment of the game's image (its address, size, flags: 1 execute, 2 write, 4 read), for the
 * co-op code's memory checks. Called for each segment before bbcoop_install. */
void bbcoop_note_segment(uint64_t address, uint64_t size, unsigned flags);

/* The co-op patches and hooks go into the eboot (image offset 0, eboot_size bytes) when
 * SHADPS4_BLOODBORNE_SEAMLESS_COOP=1 (SHADPS4_BLOODBORNE_RE_TRACE=1: the trace hooks). Called
 * after relocation and the image patches, while the image is still writable. The hooks'
 * trampolines go to `code`, executable memory within 2 GiB of the image. */
void bbcoop_install(unsigned char *image, uint64_t eboot_size, void *code, uint64_t code_size,
                    const char *app0);

/* A fatal exception (Windows: its EXCEPTION_POINTERS) is ending the process: the crash report
 * (user/log/crash-*.txt and .dmp). */
void bbcoop_crash_report(void *exception_pointers);

/* The window is closing: a game whose thread has stopped ticking writes a freeze report
 * (user/log/freeze-*.txt and .dmp) first. */
void bbcoop_window_closing(void);

/* Autoplay (SHADPS4_BLOODBORNE_AUTOPLAY, the rig's scripted tests): the PS4 pad buttons and the
 * sticks (lx, ly, rx, ry; 128 is centred) it holds now; 0 when it is off. */
int bbcoop_autoplay_pad(uint32_t *buttons, uint8_t sticks[4]);

/* Party death (always on in the port): sees what the game will
 * read from the pad (the give-up hold, attacks that revive a downed member) and, while the player
 * is down, lets go of all but Options and the camera stick. */
void bbcoop_party_death_pad(uint32_t *buttons, uint8_t sticks[4], uint8_t *l2, uint8_t *r2);

/* Party death's panel, for the overlay: the other party members playing it (online id, state,
 * HP, revive progress while down; `here` on this player's map), whom an attack would revive now
 * (an index into members, or -1) and this player's own down. */
enum { BBCOOP_VITAL_OFF, BBCOOP_VITAL_UP, BBCOOP_VITAL_DOWN, BBCOOP_VITAL_DEAD };
enum { BBCOOP_PANEL_MEMBERS = 4 };
typedef struct bbcoop_panel_member {
    char name[20]; /* "" when unknown */
    int vital;     /* BBCOOP_VITAL_* */
    int here;
    int32_t hp, hp_max;
    uint32_t landed, needed;
} bbcoop_panel_member;
typedef struct bbcoop_panel {
    int member_count;
    bbcoop_panel_member members[BBCOOP_PANEL_MEMBERS];
    int revive_target;
    int vital; /* this player's */
    uint32_t landed, needed;
    float give_up; /* the give-up hold, 0 to 1 */
    int native_list; /* the game's HUD shows the party list (the party HUD's CoopParty tags) */
    int native_prompt; /* the game's HUD shows the prompt (CoopPrompt): the overlay draws its glyph only */
} bbcoop_panel;
/* Fills `panel`; 0 when there is nothing to draw (party death off, no party session, nobody else
 * on the vitals board and this player not down). */
int bbcoop_party_panel(bbcoop_panel *panel);
/* A revive's progress, 0 to 1: the share of the damage it needs that has landed (`landed`
 * and `needed` from the panel). */
float bbcoop_revive_share(uint32_t landed, uint32_t needed);
/* Where a file of the player's game folder (app0) is on this PC, `relative` like
 * "dvdroot_ps4/menu/common.tpf.dcx"; 0 before the game is installed or when `out` is too small. */
int bbcoop_game_file(const char *relative, char *out, size_t size);

/* Party portraits (the co-op code's bloodborne_portrait.h). The texture cache read back the status
 * page's model (its render target, `width` x `height` RGBA rows): this character's portrait once
 * the page closes. */
void bbcoop_portrait_model(const uint8_t *rgba, int width, int height);
/* The party list's portrait slot `slot` (0-2, the party HUD's CoopPartyFace): its version, which
 * changes whenever its picture does; with the picture (64x64 RGBA into `rgba`, `size` bytes) 1, 0
 * when it shows none. */
uint32_t bbcoop_portrait_version(int slot);
/* The off-screen status model is drawn (TickPortraits): read the 560x768 model target back as it
 * is drawn into, not only when the status page samples it. */
int bbcoop_portrait_offscreen_wanted(void);
int bbcoop_portrait_slot(int slot, uint8_t *rgba, size_t size, uint32_t *version);

/* The game opens a file (its guest path): the party code notes the character's save slot. */
void bbcoop_file_opened(const char *guest_path);

/* The game unlocked a trophy itself; party members get it too. */
void bbcoop_trophy_unlocked(int32_t trophy_id);

/* Phase 5, the team lobby (always on with co-op), for the game's own menu
 * (the team screen, bbport_overlay.cpp). Names are UTF-8; `id` is what a request names (a
 * player's lobby member, a team's id). */
enum { BBCOOP_LOBBY_TEAM = 4, BBCOOP_LOBBY_PLAYERS = 16, BBCOOP_LOBBY_INVITES = 8 };
enum { BBCOOP_LOBBY_OFFLINE, BBCOOP_LOBBY_TITLE, BBCOOP_LOBBY_PLAYING };
typedef struct bbcoop_lobby_entry {
    char name[40];
    char id[40];
    int state; /* BBCOOP_LOBBY_*: a player's or a member's */
    int ready; /* a member's */
} bbcoop_lobby_entry;
typedef struct bbcoop_lobby {
    int signed_in; /* the server answered */
    int in_team, leader, started;
    int member_count, player_count, invite_count;
    bbcoop_lobby_entry members[BBCOOP_LOBBY_TEAM];       /* the leader first */
    bbcoop_lobby_entry players[BBCOOP_LOBBY_PLAYERS];    /* others signed in, not in this team */
    bbcoop_lobby_entry invites[BBCOOP_LOBBY_INVITES];    /* name: who invited, id: the team */
    char notice[96];                                      /* the server's last refusal */
    char me[40];                                          /* this player's id */
} bbcoop_lobby;
/* Fills `lobby`; 0 when the team lobby is off. */
int bbcoop_lobby_view(bbcoop_lobby *lobby);
/* The menu's requests (TeamRequest actions): create, leave, invite a player (`id`), accept or
 * decline an invite (`id`: its team), remove a member (`id`). */
enum { BBCOOP_TEAM_CREATE = 1, BBCOOP_TEAM_INVITE = 2, BBCOOP_TEAM_ACCEPT = 3,
       BBCOOP_TEAM_DECLINE = 4, BBCOOP_TEAM_LEAVE = 5, BBCOOP_TEAM_REMOVE = 6 };
void bbcoop_lobby_request(int action, const char *id);
/* The title holds a save's load for the team: the team screen is up (bbport_overlay.cpp). */
int bbcoop_team_screen(void);
/* Its buttons: this character ready, not ready, the leader's start (a member's: ready), alone. */
enum { BBCOOP_TEAM_GO_READY = 0, BBCOOP_TEAM_GO_UNREADY = 1, BBCOOP_TEAM_GO_START = 2,
       BBCOOP_TEAM_GO_ALONE = 3 };
void bbcoop_team_choose(int what);

/* Trading between party members (the co-op code's bloodborne_trade.h, with
 * always on in the port with the server world): what the port's trade menus (shim/bbport_trade_menu.cpp) need. An
 * item is a kind (1 Blood Echoes with id 0, 2 goods by EquipParamGoods id); texts are UTF-8 and
 * `size` bytes at most. Calls from the game's thread. */
int bbcoop_trade_on(void);
int bbcoop_trade_tradeable(uint32_t kind, uint32_t id);
uint32_t bbcoop_trade_held(uint32_t kind, uint32_t id);
/* The quantity picker's line for giving the item to the nearest member on this map; 0 when
 * nobody is there to give to. */
int bbcoop_trade_give_question(uint32_t kind, uint32_t id, char *text, size_t size);
/* Offers `count` of the item to the nearest member (taken out at once): 1 when sent. */
int bbcoop_trade_offer(uint32_t kind, uint32_t id, uint32_t count);
/* The oldest offer to this player waiting for an answer: its id and question; 0 when none. */
int bbcoop_trade_ask(uint64_t *id, char *text, size_t size);
/* Whether offer `id` still waits for this player's answer. */
int bbcoop_trade_ask_open(uint64_t id);
/* Accepting takes what was asked for out first: 1 when it was a yes that went. */
int bbcoop_trade_answer(uint64_t id, int accept);
/* The next notice to show, skipping those with nothing to say; 0 when none. */
int bbcoop_trade_notice(char *text, size_t size);
/* The menus' tick: the co-op code runs it on the game's thread with its trades. */
void bbcoop_trade_set_ui_tick(void (*tick)(void));

#ifdef __cplusplus
}
#endif
