/* Save data read watch (diagnostic, Windows only, Experimental tree).
 *
 * Purpose: find which bytes of the loaded save the game looks at while a screen is built (for
 * example which flags decide that a Situation Battle is unlocked), without reverse engineering the
 * code. F10 arms the watch, F10 again stops it and writes out/.../user/savewatch.txt.
 *
 * How: on arming, the host copy of GAMESAVEFILE.DAT is searched for in the process's memory (the
 * game keeps its loaded save as a plain struct). The pages of every copy found are made
 * PAGE_NOACCESS. Each access faults; the handler logs the byte offset inside the save and the
 * guest code address, lets the one instruction run (trap flag) and protects the page again.
 * While armed the game is slow. Only PS4RT_SAVE_WATCH=1 enables the key. */
#ifdef _WIN32
#define PSAPI_VERSION 2
#include <windows.h>
#include <psapi.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include "runtime.h"

#define MAX_COPIES 8
#define MAX_EVENTS 60000
#define PROBES 3

typedef struct { uintptr_t base; size_t size; } Copy;
typedef struct { uint32_t offset; uint32_t rip_rva; uint32_t count; uint32_t order; uint8_t write; uint8_t copy; } Event;

static volatile LONG armed;
static Copy copies[MAX_COPIES];
static int copy_count;
static uint8_t *file_data;
static size_t file_size;
static Event *events;
static uint32_t event_count, order_counter;
static uint64_t dropped;
static volatile LONG64 faults;
static volatile LONG runaway;
#define MAX_FAULTS 400000 /* beyond this the game is hammering the save: stop instead of freezing */
static uint32_t *table; /* open addressing: event index + 1 */
#define TABLE_SIZE 262144u
static CRITICAL_SECTION lock;
static int lock_ready;
static DWORD tls_pending = TLS_OUT_OF_INDEXES; /* page to protect again after the single step */

/* Every page that is currently protected, with its original protection. */
typedef struct { uintptr_t page; DWORD old; } Guarded;
static Guarded *guarded;
static size_t guarded_count, guarded_cap;

/* Buffers the game read GAMESAVEFILE.DAT into (recorded by runtime_file.c). */
#define MAX_READS 6
static struct { uintptr_t addr; size_t size; } reads[MAX_READS];
static int read_count;
void runtime_savewatch_note_read(const void *buffer, size_t size) {
    if (size < 1000000 || !buffer) return;
    if (read_count < MAX_READS) { reads[read_count].addr = (uintptr_t)buffer; reads[read_count].size = size; ++read_count; }
    printf("Runtime: save watch: save read into %p (%zu bytes)\n", buffer, size);
}
static void check_reads(const size_t *probe, int probes) {
    for (int r = 0; r < read_count && copy_count < MAX_COPIES; ++r) {
        uintptr_t base = reads[r].addr;
        int ok = reads[r].size == file_size;
        MEMORY_BASIC_INFORMATION mbi;
        if (ok) ok = VirtualQuery((void *)base, &mbi, sizeof(mbi)) && mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS));
        int match = 0;
        if (ok) {
            match = 1;
            for (int i = 0; i < probes; ++i) if (memcmp((void *)(base + probe[i]), file_data + probe[i], 32)) { match = 0; break; }
        }
        printf("Runtime: save watch: read buffer %p %s\n", (void *)base, match ? "still holds the save" : "no longer holds the save");
        if (!match) continue;
        int dup = 0;
        for (int c = 0; c < copy_count; ++c) if (copies[c].base == base) dup = 1;
        if (dup) continue;
        copies[copy_count].base = base; copies[copy_count].size = file_size; ++copy_count;
    }
}

static int find_save_file(char *out, size_t size) {
    char base[600];
    snprintf(base, sizeof(base), "%s/savedata", runtime_file_user_dir());
    time_t best = 0;
    DIR *users = opendir(base);
    if (!users) return 0;
    struct dirent *u;
    while ((u = readdir(users))) {
        if (u->d_name[0] == '.') continue;
        char p1[700];
        snprintf(p1, sizeof(p1), "%s/%s", base, u->d_name);
        DIR *titles = opendir(p1);
        if (!titles) continue;
        struct dirent *t;
        while ((t = readdir(titles))) {
            if (t->d_name[0] == '.') continue;
            char path[1000];
            snprintf(path, sizeof(path), "%s/%s/GAMESAVE/GAMESAVEFILE.DAT", p1, t->d_name);
            struct stat st;
            if (!stat(path, &st) && st.st_mtime >= best) { best = st.st_mtime; snprintf(out, size, "%s", path); }
        }
        closedir(titles);
    }
    closedir(users);
    return best != 0;
}

static int load_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return 0; }
    free(file_data);
    file_data = malloc((size_t)n);
    file_size = file_data && fread(file_data, 1, (size_t)n, f) == (size_t)n ? (size_t)n : 0;
    fclose(f);
    return file_size != 0;
}

/* Windows of 32 bytes with many distinct non-zero values: unlikely to occur by chance and not
 * the kind of field a game changes right after loading. */
static int pick_probes(size_t *offsets) {
    int got = 0;
    for (size_t o = 4096; o + 32 < file_size && got < PROBES; o += 32768) {
        for (size_t at = o; at < o + 32768 && at + 32 < file_size; at += 16) {
            int seen[256] = {0}, distinct = 0;
            for (int i = 0; i < 32; ++i) if (file_data[at + i] && !seen[file_data[at + i]]++) ++distinct;
            if (distinct >= 8) { offsets[got++] = at; break; }
        }
    }
    return got;
}

static void guard_add(uintptr_t page, DWORD old) {
    if (guarded_count == guarded_cap) {
        guarded_cap = guarded_cap ? guarded_cap * 2 : 1024;
        guarded = realloc(guarded, guarded_cap * sizeof(*guarded));
    }
    guarded[guarded_count].page = page;
    guarded[guarded_count].old = old;
    ++guarded_count;
}
static Guarded *guard_find(uintptr_t page) {
    for (size_t i = 0; i < guarded_count; ++i) if (guarded[i].page == page) return &guarded[i];
    return NULL;
}

/* Only pages that are already resident are read: touching committed-but-unused guest memory would
 * make Windows back several GB of zero pages and can freeze the machine. */
static void scan_run(uintptr_t run, uintptr_t page, const size_t *probe, int probes) {
    for (uintptr_t p = run; p + 32 <= page; ) {
        const uint8_t *hit = memchr((const void *)p, file_data[probe[0]], page - 32 - p + 1);
        if (!hit) break;
        p = (uintptr_t)hit + 1;
        if (memcmp(hit, file_data + probe[0], 32)) continue;
        uintptr_t base = (uintptr_t)hit - probe[0];
        if (base < run || base + file_size > page || base == (uintptr_t)file_data) continue;
        int match = 1;
        for (int i = 1; i < probes; ++i) if (memcmp((void *)(base + probe[i]), file_data + probe[i], 32)) { match = 0; break; }
        if (!match || copy_count >= MAX_COPIES) continue;
        copies[copy_count].base = base;
        copies[copy_count].size = file_size;
        ++copy_count;
        p = base + file_size;
    }
}
#define BATCH 2048
static void search_runs(uintptr_t rb, uintptr_t re, const size_t *probe, int probes) {
    static PSAPI_WORKING_SET_EX_INFORMATION info[BATCH];
    uintptr_t run = 0;
    for (uintptr_t base = rb; base < re && copy_count < MAX_COPIES; base += (uintptr_t)BATCH * 4096) {
        size_t n = (re - base) / 4096;
        if (n > BATCH) n = BATCH;
        for (size_t i = 0; i < n; ++i) { info[i].VirtualAddress = (void *)(base + i * 4096); info[i].VirtualAttributes.Flags = 0; }
        if (!K32QueryWorkingSetEx(GetCurrentProcess(), info, (DWORD)(n * sizeof(info[0])))) { run = 0; continue; }
        for (size_t i = 0; i < n; ++i) {
            uintptr_t page = base + i * 4096;
            if (info[i].VirtualAttributes.Flags & 1) { if (!run) run = page; continue; }
            if (run && page - run >= file_size) scan_run(run, page, probe, probes);
            run = 0;
        }
    }
    if (run && re - run >= file_size) scan_run(run, re, probe, probes);
}
static void search_memory(const size_t *probe, int probes) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    uintptr_t at = (uintptr_t)si.lpMinimumApplicationAddress, end = (uintptr_t)si.lpMaximumApplicationAddress;
    const DWORD started = GetTickCount();
    MEMORY_BASIC_INFORMATION mbi;
    while (at < end && VirtualQuery((void *)at, &mbi, sizeof(mbi))) {
        uintptr_t rb = (uintptr_t)mbi.BaseAddress, re = rb + mbi.RegionSize;
        at = re;
        if (GetTickCount() - started > 20000) { printf("Runtime: save watch: search stopped after 20 s\n"); return; }
        if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) || !(mbi.Protect & (PAGE_READWRITE | PAGE_READONLY | PAGE_WRITECOPY)))
            continue;
        if (mbi.RegionSize < file_size) continue;
        search_runs(rb, re, probe, probes);
    }
}

static void protect_copies(void) {
    for (int c = 0; c < copy_count; ++c) {
        uintptr_t first = copies[c].base & ~(uintptr_t)4095, last = (copies[c].base + copies[c].size - 1) & ~(uintptr_t)4095;
        for (uintptr_t page = first; page <= last; page += 4096) {
            if (guard_find(page)) continue;
            DWORD old = 0;
            if (VirtualProtect((void *)page, 4096, PAGE_NOACCESS, &old)) guard_add(page, old);
        }
    }
}
static void unprotect_all(void) {
    for (size_t i = 0; i < guarded_count; ++i) {
        DWORD ignore;
        VirtualProtect((void *)guarded[i].page, 4096, guarded[i].old, &ignore);
    }
    guarded_count = 0;
}

static void record(uintptr_t addr, uintptr_t rip, int write) {
    for (int c = 0; c < copy_count; ++c) {
        if (addr < copies[c].base || addr >= copies[c].base + copies[c].size) continue;
        uint32_t off = (uint32_t)(addr - copies[c].base);
        uint32_t rva = (uint32_t)(rip - runtime_image_base);
        EnterCriticalSection(&lock);
        uint32_t h = (off * 2654435761u ^ rva * 40503u ^ (uint32_t)c * 977u ^ (uint32_t)write) & (TABLE_SIZE - 1);
        for (;;) {
            if (!table[h]) {
                if (event_count < MAX_EVENTS) {
                    events[event_count] = (Event){off, rva, 1, order_counter++, (uint8_t)write, (uint8_t)c};
                    table[h] = ++event_count;
                } else ++dropped;
                break;
            }
            Event *e = &events[table[h] - 1];
            if (e->offset == off && e->copy == c && e->rip_rva == rva && e->write == write) { ++e->count; break; }
            h = (h + 1) & (TABLE_SIZE - 1);
        }
        LeaveCriticalSection(&lock);
        return;
    }
}

int runtime_savewatch_fault(EXCEPTION_POINTERS *info) {
    const EXCEPTION_RECORD *rec = info->ExceptionRecord;
    if (tls_pending == TLS_OUT_OF_INDEXES) return 0;
    if (rec->ExceptionCode == EXCEPTION_SINGLE_STEP) {
        uintptr_t page = (uintptr_t)TlsGetValue(tls_pending);
        if (!page) return 0;
        TlsSetValue(tls_pending, NULL);
        info->ContextRecord->EFlags &= ~0x100u;
        if (armed) { DWORD ignore; VirtualProtect((void *)page, 4096, PAGE_NOACCESS, &ignore); }
        return 1;
    }
    if (rec->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || rec->NumberParameters < 2 || !armed) return 0;
    uintptr_t addr = (uintptr_t)rec->ExceptionInformation[1], page = addr & ~(uintptr_t)4095;
    Guarded *g = guard_find(page);
    if (!g) return 0;
    if (InterlockedIncrement64(&faults) > MAX_FAULTS) {
        /* the game reads the save in bulk (a copy or scan of the whole struct): give the pages back */
        if (!InterlockedExchange(&runaway, 1)) { armed = 0; for (size_t i = 0; i < guarded_count; ++i) { DWORD ig; VirtualProtect((void *)guarded[i].page, 4096, guarded[i].old, &ig); } }
        return 1;
    }
    record(addr, (uintptr_t)info->ContextRecord->Rip, rec->ExceptionInformation[0] == 1);
    DWORD ignore;
    VirtualProtect((void *)page, 4096, g->old, &ignore);
    TlsSetValue(tls_pending, (void *)page);
    info->ContextRecord->EFlags |= 0x100u;
    return 1;
}

static int cmp_events(const void *a, const void *b) {
    const Event *x = a, *y = b;
    if (x->copy != y->copy) return x->copy - y->copy;
    return x->offset < y->offset ? -1 : x->offset > y->offset;
}

static void write_report(void) {
    char path[700];
    snprintf(path, sizeof(path), "%s/savewatch.txt", runtime_file_user_dir());
    FILE *f = fopen(path, "w");
    if (!f) { printf("Runtime: save watch: cannot write %s\n", path); return; }
    fprintf(f, "# save watch: %u unique accesses (%llu dropped), %d copies of the save in memory, file %zu bytes\n",
            event_count, (unsigned long long)dropped, copy_count, file_size);
    for (int c = 0; c < copy_count; ++c) fprintf(f, "# copy %d at %p\n", c, (void *)copies[c].base);
    fprintf(f, "# columns: copy offset(hex) R/W count first-order guest-code-rva value-now file-value\n");
    qsort(events, event_count, sizeof(*events), cmp_events);
    for (uint32_t i = 0; i < event_count; ++i) {
        const Event *e = &events[i];
        uint8_t now = *(volatile uint8_t *)(copies[e->copy].base + e->offset);
        fprintf(f, "%d %06x %c %u %u %08x %02x %02x\n", e->copy, e->offset, e->write ? 'W' : 'R', e->count, e->order,
                e->rip_rva, now, e->offset < file_size ? file_data[e->offset] : 0);
    }
    fclose(f);
    printf("Runtime: save watch: wrote %s (%u accesses)\n", path, event_count);
}

void runtime_savewatch_toggle(void) {
    static int enabled = -1;
    if (enabled < 0) { const char *e = getenv("PS4RT_SAVE_WATCH"); enabled = e && *e && *e != '0'; }
    if (!enabled) return;
    if (!lock_ready) { InitializeCriticalSection(&lock); lock_ready = 1; }
    if (tls_pending == TLS_OUT_OF_INDEXES) tls_pending = TlsAlloc();
    if (armed || runaway) {
        if (runaway) printf("Runtime: save watch had stopped itself (more than %d faults: the game reads the save in bulk); writing what was recorded\n", MAX_FAULTS);
        armed = 0;
        Sleep(50);
        unprotect_all();
        write_report();
        runaway = 0;
        return;
    }
    char path[1000];
    if (!find_save_file(path, sizeof(path)) || !load_file(path)) { printf("Runtime: save watch: no GAMESAVEFILE.DAT found under %s/savedata\n", runtime_file_user_dir()); return; }
    size_t probe[PROBES];
    int probes = pick_probes(probe);
    if (probes < 2) { printf("Runtime: save watch: save file too empty to search for\n"); return; }
    copy_count = 0;
    printf("Runtime: save watch: searching memory for the save (resident pages only)...\n"); fflush(stdout);
    check_reads(probe, probes);
    if (!copy_count) search_memory(probe, probes);
    faults = 0; runaway = 0;
    if (!copy_count) { printf("Runtime: save watch: the save is not in memory as a plain copy (%s)\n", path); return; }
    free(events);
    events = calloc(MAX_EVENTS, sizeof(Event));
    free(table);
    table = calloc(TABLE_SIZE, sizeof(*table));
    event_count = order_counter = 0;
    dropped = 0;
    protect_copies();
    armed = 1;
    printf("Runtime: save watch ARMED on %d copies of %s; F10 again to stop and write the report\n", copy_count, path);
}
#else
void runtime_savewatch_toggle(void) {}
void runtime_savewatch_note_read(const void *buffer, size_t size) { (void)buffer; (void)size; }
#endif
