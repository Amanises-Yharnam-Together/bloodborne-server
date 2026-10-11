/* Save data metadata (param.bin, icon0.png) is replaced whole: written beside the old file, then
 * renamed over it, so a crash mid-write keeps the old one. The second write of a file must
 * replace the first (Windows: the CRT's rename fails when the target exists).
 * After configuring the GPU build: ninja -C out/gpu savedata-replace-test
 * Then run: out/gpu/savedata-replace-test.exe */
#include "../src/runtime_savedata.c"
#include <assert.h>
#ifdef _WIN32
#include <windows.h>
#endif

const char *runtime_file_user_dir(void) { return "."; }
int runtime_file_mount(const char *guest, const char *host) { (void)guest; (void)host; return 0; }
void runtime_file_unmount(const char *guest) { (void)guest; }
/* runtime_file.c's replace, as it is for a file nobody else holds. */
int runtime_file_replace_host(const char *from, const char *to) {
    return MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) ? 0 : -1;
}
uintptr_t runtime_lookup(const RuntimeExport *table, size_t n, const char *name) {
    for (size_t i = 0; i < n; ++i)
        if (!strcmp(table[i].name, name)) return (uintptr_t)table[i].function;
    return 0;
}

static void read_back(const char *path, char *out, size_t size) {
    FILE *f = fopen(path, "rb");
    assert(f);
    size_t n = fread(out, 1, size - 1, f);
    out[n] = 0;
    fclose(f);
}

int main(void) {
    char dir[512], path[600], temp[620], text[64];
#ifdef _WIN32
    char base[MAX_PATH];
    DWORD length = GetTempPathA(sizeof(base), base);
    assert(length > 0 && length < sizeof(base));
    snprintf(dir, sizeof(dir), "%sbb-savedata-replace-%lu", base, (unsigned long)GetCurrentProcessId());
#else
    snprintf(dir, sizeof(dir), "/tmp/bb-savedata-replace-%d", (int)getpid());
#endif
    assert(!mkdir(dir, 0700));
    snprintf(path, sizeof(path), "%s/param.bin", dir);
    snprintf(temp, sizeof(temp), "%s.bbtmp", path);

    assert(!write_atomic(path, "first", 5));
    read_back(path, text, sizeof(text));
    assert(!strcmp(text, "first"));
    /* The file exists now: the next write replaces it. */
    assert(!write_atomic(path, "second save", 11));
    read_back(path, text, sizeof(text));
    assert(!strcmp(text, "second save"));
    struct stat st;
    assert(stat(temp, &st) != 0); /* no temporary left behind */

    remove(path);
    rmdir(dir);
    puts("savedata replace: ok");
    return 0;
}
