/* The HLE runtime on Windows, linked as bb-probe links it (no game, no Vulkan device):
 * - thread handles (ScePthread) fit 40 bits: the game packs them into 40-bit fields, and a
 *   handle above that never matches its own lock (lamp travel hung at loading).
 * After configuring the GPU build: ninja -C out/gpu runtime-windows-test && out/gpu/runtime-windows-test.exe */
#include "runtime.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The settings menu of the GPU library restarts through probe.c, which tests do not link. */
void runtime_restart(void) { abort(); }
uint32_t runtime_thread_tcb_offset(void);

typedef void *(ABI *ThreadSelf)(void);
#define GET(type, name) ((type)runtime_resolve(name, 0))

static void *worker(void *unused) {
    (void)unused;
    return GET(ThreadSelf, "aI+OeCz8xrQ#p#J")();
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    runtime_memory_reserve();
    runtime_thread_tcb_offset();
    runtime_start(1);
    void *self = GET(ThreadSelf, "aI+OeCz8xrQ#p#J")();
    assert(self);
    printf("main thread handle %p\n", self);
    assert(!((uintptr_t)self >> 40));
    for (int i = 0; i < 64; ++i) {
        pthread_t thread;
        void *handle = NULL;
        assert(pthread_create(&thread, NULL, worker, NULL) == 0);
        assert(pthread_join(thread, &handle) == 0);
        assert(handle && handle != self);
        assert(!((uintptr_t)handle >> 40));
    }
    puts("thread handles fit 40 bits: ok");
    puts("PASS: runtime on Windows");
    return 0;
}
