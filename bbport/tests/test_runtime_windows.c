/* The HLE runtime on Windows, linked as bb-probe links it (no game, no Vulkan device):
 * - thread handles (ScePthread) fit 40 bits: the game packs them into 40-bit fields, and a
 *   handle above that never matches its own lock (lamp travel hung at loading);
 * - a debug exception raised by the guest stops the process without exit handlers or static
 *   destructors, which would run under the game's still running threads.
 * A failed check exits at once (no abort(): its report could wait for a click).
 * After configuring the GPU build: ninja -C out/gpu runtime-windows-test && out/gpu/runtime-windows-test.exe */
#include "runtime.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* The settings menu of the GPU library restarts through probe.c, which tests do not link. */
void runtime_restart(void) { abort(); }
uint32_t runtime_thread_tcb_offset(void);

#define CHECK(condition)                                                              \
    do {                                                                              \
        if (!(condition)) {                                                           \
            printf("%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition);      \
            fflush(stdout);                                                           \
            _exit(1);                                                                 \
        }                                                                             \
    } while (0)

typedef void *(ABI *ThreadSelf)(void);
typedef void (ABI *Raise)(uint32_t, uint64_t);
#define GET(type, name) ((type)runtime_resolve(name, 0))

static void *worker(void *unused) {
    (void)unused;
    return GET(ThreadSelf, "aI+OeCz8xrQ#p#J")();
}
static void atexit_ran(void) { puts("atexit handler ran"); }

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    runtime_memory_reserve();
    runtime_thread_tcb_offset();
    runtime_start(1);
    if (argc > 1 && !strcmp(argv[1], "--raise-exception")) {
        atexit(atexit_ran);
        GET(Raise, "OMDRKKAZ8I4#p#J")(1, 2);
        return 99;
    }
    void *self = GET(ThreadSelf, "aI+OeCz8xrQ#p#J")();
    CHECK(self);
    printf("main thread handle %p\n", self);
    CHECK(!((uintptr_t)self >> 40));
    for (int i = 0; i < 64; ++i) {
        pthread_t thread;
        void *handle = NULL;
        CHECK(pthread_create(&thread, NULL, worker, NULL) == 0);
        CHECK(pthread_join(thread, &handle) == 0);
        CHECK(handle && handle != self);
        CHECK(!((uintptr_t)handle >> 40));
    }
    /* The records so far left the start of low memory to the game's image (0x800000000, where
     * the guest code hooks expect it). */
    void *image = runtime_low_map(65536, PROT_READ | PROT_WRITE);
    printf("image placed at %p\n", image);
    CHECK((uintptr_t)image == UINT64_C(0x800000000));
    runtime_image_start = (uintptr_t)image;
    /* After it: records from low memory. */
    for (int i = 0; i < 64; ++i) {
        pthread_t thread;
        void *handle = NULL;
        CHECK(pthread_create(&thread, NULL, worker, NULL) == 0);
        CHECK(pthread_join(thread, &handle) == 0);
        CHECK((uintptr_t)handle > (uintptr_t)image && (uintptr_t)handle < UINT64_C(0x1000000000));
    }
    puts("thread handles fit 40 bits: ok");
    /* The exit path in a child: rc 23, no exit handlers. */
    char command[1024];
    snprintf(command, sizeof(command), "\"\"%s\" --raise-exception > runtime-windows-raise.log 2>&1\"", argv[0]);
    const int rc = system(command);
    FILE *log = fopen("runtime-windows-raise.log", "rb");
    CHECK(log);
    char text[4096] = {0};
    const size_t n = fread(text, 1, sizeof(text) - 1, log);
    fclose(log);
    remove("runtime-windows-raise.log");
    text[n] = 0;
    printf("raise exception: rc=%d\n", rc);
    CHECK(rc == 23);
    CHECK(strstr(text, "guest raised debug exception"));
    CHECK(!strstr(text, "atexit handler ran"));
    puts("raised exception skips exit handlers: ok");
    puts("PASS: runtime on Windows");
    fflush(stdout);
    /* The runtime's threads and handlers stay as they are: no exit handlers here either. */
    _exit(0);
}
