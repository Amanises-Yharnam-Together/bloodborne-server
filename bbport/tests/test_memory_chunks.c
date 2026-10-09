/* The runtime's direct memory chunks (BbGuestMemory, the PC memory model): the GPU library's chunk
 * allocator is asked once for each new whole-allocation region (BB_LAYER_MEMORY, NVIDIA) or each
 * 256 MiB grid chunk, with the backing view of that memory there to import; released memory reads
 * as zero again. Windows: the chunks stay in the runtime's section and are imported in place
 * (CHUNK_IMPORTED). No GPU: the allocator here records the calls.
 * After configuring the GPU build: ninja -C out/gpu memory-chunks-test && out/gpu/memory-chunks-test.exe */
#include "../src/runtime_memory.c"
#include <assert.h>

static int32_t guest_errno;
int32_t *runtime_errno(void) { return &guest_errno; }
void runtime_guest_call_sites(uint64_t out[3]) { out[0] = out[1] = out[2] = 0; }
uintptr_t runtime_lookup(const RuntimeExport *table, size_t n, const char *name) {
    for (size_t i = 0; i < n; ++i)
        if (!strcmp(table[i].name, name)) return (uintptr_t)table[i].function;
    return 0;
}

typedef struct { uint64_t phys, size; } Call;
static Call calls[16];
static int call_count;
static int answer; /* what the allocator returns: CHUNK_IMPORTED (-3) or -1 (refused) */

static int record_chunk(uint64_t phys, uint64_t size) {
    assert(call_count < 16);
    calls[call_count++] = (Call){phys, size};
    /* The GPU library imports the backing view of the chunk (AllocImported). */
    unsigned char *backing = runtime_memory_backing_pointer(phys);
    assert(backing && !((uintptr_t)backing % 4096));
    return answer;
}

#define MIB (UINT64_C(1) << 20)

int main(void) {
    runtime_memory_set_guest_chunk_whole(1);
    runtime_memory_set_guest_chunk_allocator(record_chunk);
    answer = -3;

    /* Whole-allocation regions: one chunk per new direct allocation. */
    int64_t a = -1, b = -1;
    assert(!direct_allocate(0, (int64_t)POOL_SIZE, MIB, 0, 0, &a) && a == 0);
    assert(call_count == 1 && calls[0].phys == 0 && calls[0].size == MIB);
    assert(!direct_allocate(0, (int64_t)POOL_SIZE, 2 * MIB, 0, 0, &b) && b == (int64_t)MIB);
    assert(call_count == 2 && calls[1].phys == MIB && calls[1].size == 2 * MIB);
    /* Memory handed out again inside a region keeps its chunk: no new one. */
    assert(!direct_release((uint64_t)a, MIB));
    int64_t again = -1;
    assert(!direct_allocate(0, (int64_t)POOL_SIZE, MIB / 2, 0, 0, &again) && again == a);
    assert(call_count == 2);
    /* Both recorded as imported chunks (the GPU uses their memory in place). */
    assert(chunk_region_count == 2 && chunk_regions[0].fd == CHUNK_IMPORTED &&
           chunk_regions[1].phys == MIB && chunk_regions[1].fd == CHUNK_IMPORTED);

    /* The game's view and the backing view the GPU imports are the same memory. */
    void *guest = NULL;
    assert(!direct_map(&guest, 2 * MIB, 3, 0, b, 0) && guest);
    unsigned char *backing = runtime_memory_backing_pointer((uint64_t)b);
    ((volatile unsigned char *)guest)[5] = 0x42;
    assert(backing[5] == 0x42);
    /* Released memory of a GPU chunk is cleared in place (the GPU holds those pages). */
    assert(!direct_release((uint64_t)b, 2 * MIB));
    assert(backing[5] == 0);

    /* The 256 MiB grid (dma-buf chunks that map at any offset): a refused chunk stays runtime
     * memory and is not asked for again. */
    runtime_memory_set_guest_chunk_whole(0);
    answer = -1;
    int64_t c = -1, d = -1;
    assert(!direct_allocate((int64_t)(512 * MIB), (int64_t)POOL_SIZE, MIB, 0, 0, &c) && c == (int64_t)(512 * MIB));
    assert(call_count == 3 && calls[2].phys == 512 * MIB && calls[2].size == 256 * MIB);
    assert(!direct_allocate((int64_t)(512 * MIB), (int64_t)POOL_SIZE, MIB, 0, 0, &d) && d == (int64_t)(513 * MIB));
    assert(call_count == 3 && chunk_fds[2] == -2 && chunk_fds[0] == -1);
    /* An imported grid chunk. */
    answer = -3;
    int64_t e = -1;
    assert(!direct_allocate((int64_t)(768 * MIB), (int64_t)POOL_SIZE, MIB, 0, 0, &e) && e == (int64_t)(768 * MIB));
    assert(call_count == 4 && calls[3].phys == 768 * MIB && chunk_fds[3] == CHUNK_IMPORTED);
    puts("memory chunks: ok");
    return 0;
}
