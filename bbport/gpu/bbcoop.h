/* SPDX-License-Identifier: GPL-2.0-or-later */
/* bbport co-op: the emulator's seamless co-op (../emulator/src/core/bloodborne_*.cpp) in the
 * native port. What the loader and the C runtime call; gpu/bbcoop/ implements it. */
#pragma once
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

/* Autoplay (SHADPS4_BLOODBORNE_AUTOPLAY, the rig's scripted tests): the PS4 pad buttons and the
 * sticks (lx, ly, rx, ry; 128 is centred) it holds now; 0 when it is off. */
int bbcoop_autoplay_pad(uint32_t *buttons, uint8_t sticks[4]);

/* The game opens a file (its guest path): the party code notes the character's save slot. */
void bbcoop_file_opened(const char *guest_path);

/* The game unlocked a trophy itself; party members get it too. */
void bbcoop_trophy_unlocked(int32_t trophy_id);

#ifdef __cplusplus
}
#endif
