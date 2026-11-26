/* Focused runtime contracts. The real DS3 boot gate is ds3-smoke, never this fixture. */
#define _GNU_SOURCE
#include "hle.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

static unsigned entered;
static void entry(CPU *c) { entered++; c->eax = 42; c->esp += 4; }
static void window_proc(CPU* c) { c->eax = 1; c->esp += 20; }
static const FuncEntry funcs[] = {{0x05001000, entry, 0}, {0x05001010, window_proc, 0}};
static const ExportEntry exports[] = {{NULL, 0}};
static Module fixture = {"fixture.exe", 0x05000000, 0x3000, 0x05001000,
                         funcs, 2, NULL, 0, exports};
static void put32(unsigned char *p, uint32_t x) { memcpy(p, &x, 4); }

static void loader_check(const char *root) {
    unsigned char image[0x3000] = {0};
    uint32_t b = fixture.base;
    image[0] = 'M'; image[1] = 'Z'; put32(image + 0x3c, 0x80);
    memcpy(image + 0x80, "PE\0\0", 4); image[0x98] = 0x0b; image[0x99] = 1;
    put32(image + 0x98 + 92, 16);
    put32(image + 0x98 + 96 + 9*8, 0x200); put32(image + 0x98 + 100 + 9*8, 24);
    put32(image + 0x200, b + 0x800); put32(image + 0x204, b + 0x804);
    put32(image + 0x208, b + 0x850); put32(image + 0x210, 12);
    put32(image + 0x800, 0x12345678);
    put32(image + 0x98 + 96 + 10*8, 0x300); put32(image + 0x98 + 100 + 10*8, 64);
    put32(image + 0x300, 72); put32(image + 0x340, b + 0x900); put32(image + 0x344, 1);
    put32(image + 0x900, 0x1000);
    put32(image + 0x98+112, 0xa00); put32(image + 0x98+116, 0x200);
    image[0xa0e] = 1; put32(image+0xa10, 16); put32(image+0xa14, 0x80000020);
    image[0xa2e] = 1; put32(image+0xa30, 1); put32(image+0xa34, 0x80000040);
    image[0xa4e] = 1; put32(image+0xa50, 0x409); put32(image+0xa54, 0x60);
    put32(image+0xa60, 0xb00); put32(image+0xa64, 92);
    image[0xb00] = 92; image[0xb02] = 52;
    for (unsigned k = 0; k < sizeof "VS_VERSION_INFO"; k++) image[0xb06+2*k] = "VS_VERSION_INFO"[k];
    put32(image+0xb28, 0xfeef04bd); put32(image+0xb30, 0x10002); put32(image+0xb34, 0x30004);
    char path[2048]; snprintf(path, sizeof path, "%s/image.bin", root);
    FILE *f = fopen(path, "wb"); assert(f);
    assert(fwrite(image, 1, sizeof image, f) == sizeof image); fclose(f);
    rt_expect_image_crc(rt_crc32(image, sizeof image));
    rt_load_module(&fixture, path);
    hle_register_module(&fixture, "C:\\DeadSpace3\\fixture.exe");
    assert(rt_call(&g_cpu, fixture.entry, 0, NULL) == 42 && entered == 1);
    assert(hle_call("kernel32.dll", "GetModuleHandleW", 1, (uint32_t[]){0}) == b);
    assert(!strcmp(hle_module_path(0), "C:\\DeadSpace3\\fixture.exe"));
    uint32_t fn = gstrdup("fixture.exe"), vb = ghalloc(92, 1), vp = ghalloc(8, 1);
    assert(hle_call("version.dll", "GetFileVersionInfoSizeA", 2, (uint32_t[]){fn, 0}) == 92);
    assert(!hle_call("version.dll", "GetFileVersionInfoA", 4, (uint32_t[]){fn, 0, 91, vb}));
    assert(hle_call("version.dll", "GetFileVersionInfoA", 4, (uint32_t[]){fn, 0, 92, vb}));
    assert(hle_call("version.dll", "VerQueryValueA", 4, (uint32_t[]){vb, gstrdup("\\"), vp, vp+4}));
    assert(rd32(vp+4) == 52 && rd32(rd32(vp)+8) == 0x10002 && rd32(rd32(vp)+12) == 0x30004);
    uint32_t tls_array = rd32(g_cpu.fs_base + 0x2c), tls = rd32(tls_array);
    assert(rd32(b + 0x850) == 0 && rd32(tls) == 0x12345678 && rd32(tls + 4) == 0);
    uint32_t slot = hle_call("kernel32.dll", "TlsAlloc", 0, NULL);
    hle_call("kernel32.dll", "TlsSetValue", 2, (uint32_t[]){slot, 0xabcdef});
    assert(hle_call("kernel32.dll", "TlsGetValue", 1, &slot) == 0xabcdef);
    assert(rd32(tls_array) == tls); /* loader TLS must not alias dynamic slots */
    CPU other = g_cpu; other.fs_base = ghalloc(0x2000, 1);
    rt_static_tls_init(&other);
    uint32_t other_tls = rd32(rd32(other.fs_base + 0x2c));
    assert(other_tls != tls && rd32(other_tls) == 0x12345678);
    wr32(other_tls, 7); assert(rd32(tls) == 0x12345678);
    rt_static_tls_free(&other); assert(!rd32(other.fs_base + 0x2c)); ghfree(other.fs_base);
    assert(rt_safe_seh_handler(&fixture, b + 0x1000) == 1);
    assert(rt_safe_seh_handler(&fixture, b + 0x1100) == 0);
    unlink(path);
}
static void fs_check(const char *root) {
    char game[2048], docs[2048], path[2048], host[2048];
    snprintf(game, sizeof game, "%s/game", root); assert(!mkdir(game, 0700));
    snprintf(docs, sizeof docs, "%s/docs", root); assert(!mkdir(docs, 0700));
    snprintf(path, sizeof path, "%s/MixedCase.DAT", game);
    FILE *f = fopen(path, "wb"); assert(f); fputs("read only", f); fclose(f);
    hle_fs_layout("C:\\DeadSpace3", "C:\\DeadSpace3", 1);
    assert(hle_fs_init(game, docs, NULL));
    assert(hle_fs_host_path("c:\\deadspace3\\mixedcase.dat", host, sizeof host));
    assert(!strcmp(path, host));
    uint32_t found = ghalloc(600, 1), pattern = hle_wstr("C:\\DeadSpace3\\mixedcase.*");
    uint32_t fh = hle_call("kernel32.dll", "FindFirstFileExW", 6, (uint32_t[]){pattern, 0, found, 0, 0, 0});
    assert(fh != 0xffffffff);
    hle_wstr_utf8(found+44, host, sizeof host); assert(!strcmp(host, "MixedCase.DAT"));
    assert(hle_call("kernel32.dll", "FindClose", 1, &fh));
    assert(hle_call("kernel32.dll", "FindFirstFileExW", 6, (uint32_t[]){pattern, 2, found, 0, 0, 0}) == 0xffffffff);
    uint32_t file = gstrdup("C:\\DeadSpace3\\mixedcase.dat");
    uint32_t h = hle_call("kernel32.dll", "CreateFileA", 7,
        (uint32_t[]){file, 0x80000100, 1, 0, 3, 0, 0}); /* metadata rights cannot override read-only game root */
    assert(h != 0xffffffff);
    struct stat before_time, after_time; assert(!stat(path, &before_time));
    uint32_t stamp = ghalloc(8, 1); wr64(stamp, 116444736000000000ull + 1600000000ull*10000000ull);
    assert(!hle_call("kernel32.dll", "SetFileTime", 4, (uint32_t[]){h, 0, 0, stamp}));
    assert(rd32(g_cpu.fs_base+0x34) == 5 && !stat(path, &after_time));
    assert(before_time.st_mtim.tv_sec == after_time.st_mtim.tv_sec && before_time.st_mtim.tv_nsec == after_time.st_mtim.tv_nsec);
    assert(before_time.st_atim.tv_sec == after_time.st_atim.tv_sec && before_time.st_atim.tv_nsec == after_time.st_atim.tv_nsec);
    assert(before_time.st_ctim.tv_sec == after_time.st_ctim.tv_sec && before_time.st_ctim.tv_nsec == after_time.st_ctim.tv_nsec);
    hle_call("kernel32.dll", "CloseHandle", 1, &h);
    assert(hle_call("kernel32.dll", "CreateFileA", 7,
        (uint32_t[]){file, 0x40000000, 0, 0, 2, 0, 0}) == 0xffffffff);
    uint32_t newfile = gstrdup("C:\\DeadSpace3\\new.dat");
    assert(hle_call("kernel32.dll", "CreateFileA", 7,
        (uint32_t[]){newfile, 0x40000000, 0, 0, 1, 0, 0}) == 0xffffffff);
    assert(!hle_call("kernel32.dll", "DeleteFileA", 1, &file));
    assert(!hle_call("kernel32.dll", "SetFileAttributesA", 2, (uint32_t[]){file, 0x80}));
    assert(hle_call("kernel32.dll", "CreateFileA", 7,
        (uint32_t[]){file, 0x80000000, 0, 0, 3, 0x04000000, 0}) == 0xffffffff);
    uint32_t newdir = gstrdup("C:\\DeadSpace3\\newdir");
    assert(!hle_call("kernel32.dll", "CreateDirectoryA", 2, (uint32_t[]){newdir, 0}));
    uint32_t cwd = ghalloc(1024, 1);
    hle_call("kernel32.dll", "GetCurrentDirectoryA", 2, (uint32_t[]){1024, cwd});
    assert(!strcmp(GSTR(cwd), "C:\\DeadSpace3"));
    assert(!hle_call("kernel32.dll", "RemoveDirectoryA", 1, (uint32_t[]){gstrdup("C:\\DeadSpace3")}));
    hle_call("kernel32.dll", "GetEnvironmentVariableA", 3, (uint32_t[]){gstrdup("PATH"), cwd, 1024});
    assert(strstr(GSTR(cwd), "C:\\DeadSpace3;") && !strstr(GSTR(cwd), "MassEffect"));
    assert(!hle_call("shell32.dll", "SHGetFolderPathA", 5, (uint32_t[]){0, 0x8005, 0, 0, cwd}));
    assert(!strcmp(GSTR(cwd), "C:\\Users\\Shepard\\Documents"));
    uint32_t sub = hle_wstr("Cache");
    assert(hle_call("shell32.dll", "SHGetFolderPathAndSubDirW", 6, (uint32_t[]){0, 5, 0, 0, sub, cwd}) == 0x80070003);
    assert(!rd16(cwd));
    assert(!hle_call("shell32.dll", "SHGetFolderPathAndSubDirW", 6, (uint32_t[]){0, 0x8005, 0, 0, sub, cwd}));
    hle_wstr_utf8(cwd, host, sizeof host);
    assert(!strcmp(host, "C:\\Users\\Shepard\\Documents\\Cache"));
    char cache_host[2048];
    assert(hle_fs_host_path(host, cache_host, sizeof cache_host)); assert(!strncmp(cache_host, docs, strlen(docs)));
    assert(!rmdir(cache_host));
    uint32_t docfile = gstrdup("C:\\Users\\Shepard\\Documents\\settings.txt");
    h = hle_call("kernel32.dll", "CreateFileA", 7,
        (uint32_t[]){docfile, 0x40000000, 0, 0, 2, 0, 0});
    assert(h != 0xffffffff);
    snprintf(host, sizeof host, "%s/settings.txt", docs);
    assert(hle_call("kernel32.dll", "SetFileTime", 4, (uint32_t[]){h, 0, 0, stamp}));
    assert(!stat(host, &after_time) && after_time.st_mtim.tv_sec == 1600000000 && !after_time.st_mtim.tv_nsec);
    /* A forked child can invalidate only its inherited host descriptors, leaving
     * the parent handle intact. This forces a real futimens EBADF without hooks. */
    pid_t invalid_fd = fork(); assert(invalid_fd >= 0);
    if (!invalid_fd) {
        long limit = sysconf(_SC_OPEN_MAX);
        for (int fd = 3; fd < limit; fd++) close(fd);
        uint32_t ok = hle_call("kernel32.dll", "SetFileTime", 4, (uint32_t[]){h, 0, 0, stamp});
        _exit(!ok && rd32(g_cpu.fs_base+0x34) == 6 ? 0 : 1);
    }
    int status; assert(waitpid(invalid_fd, &status, 0) == invalid_fd);
    assert(WIFEXITED(status) && !WEXITSTATUS(status));
    hle_call("kernel32.dll", "CloseHandle", 1, &h);
    h = hle_call("kernel32.dll", "CreateFileA", 7, (uint32_t[]){docfile, 0x80000000, 0, 0, 3, 0, 0});
    assert(h != 0xffffffff && !hle_call("kernel32.dll", "SetFileTime", 4, (uint32_t[]){h, 0, 0, stamp}));
    assert(rd32(g_cpu.fs_base+0x34) == 5); hle_call("kernel32.dll", "CloseHandle", 1, &h);
    h = hle_call("kernel32.dll", "CreateFileA", 7, (uint32_t[]){docfile, 0x100, 0, 0, 3, 0, 0});
    assert(h != 0xffffffff); wr64(stamp, rd64(stamp)+10000000ull);
    assert(hle_call("kernel32.dll", "SetFileTime", 4, (uint32_t[]){h, 0, 0, stamp}));
    assert(!stat(host, &after_time) && after_time.st_mtim.tv_sec == 1600000001);
    hle_call("kernel32.dll", "CloseHandle", 1, &h);
    assert(!hle_call("kernel32.dll", "CopyFileA", 3, (uint32_t[]){docfile, newfile, 1}));
    assert(!hle_call("kernel32.dll", "MoveFileA", 2, (uint32_t[]){file, docfile}));
    assert(!hle_call("kernel32.dll", "WritePrivateProfileStringA", 4,
        (uint32_t[]){gstrdup("section"), gstrdup("key"), gstrdup("value"), newfile}));
    snprintf(host, sizeof host, "%s/settings.txt", docs); assert(!access(host, F_OK)); unlink(host);
    unlink(path); rmdir(game); rmdir(docs);
}
static void diagnostic_check(void) {
    int fd[2]; assert(!pipe(fd)); pid_t child = fork(); assert(child >= 0);
    if (!child) {
        close(fd[0]); dup2(fd[1], 2); close(fd[1]);
        push32(&g_cpu, 0x12345678);
        hle_invoke(&g_cpu, hle_resolve("unknown.dll", "#999"));
        _exit(0);
    }
    close(fd[1]); char output[8192]; size_t used = 0; ssize_t n;
    while ((n = read(fd[0], output + used, sizeof output - used - 1)) > 0) used += n;
    close(fd[0]); output[used] = 0;
    int status; assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) != 0);
    assert(strstr(output, "unsupported HLE: unknown.dll!#999 eip=12345678 tid=4100"));
}
int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--hle-inventory")) { hle_dump_inventory(); return 0; }
    if (argc == 2 && !strcmp(argv[1], "--early-exit")) {
        rt_init(); hle_call("kernel32.dll", "ExitProcess", 1, (uint32_t[]){0}); return 99;
    }
    char root[] = "/tmp/opencode/ds3-boot-XXXXXX"; assert(mkdtemp(root));
    rt_init(); loader_check(root);
    assert(!hle_call("imm32.dll", "ImmDisableIME", 1, (uint32_t[]){0}));
    assert(rd32(g_cpu.fs_base+0x34) == 50);
    uint32_t cursor = hle_call("user32.dll", "LoadCursorA", 2, (uint32_t[]){0, 32512}); assert(cursor);
    assert(hle_call("user32.dll", "LoadCursorW", 2, (uint32_t[]){0, 32512}) == cursor);
    assert(hle_call("user32.dll", "SetCursor", 1, (uint32_t[]){0}) == cursor);
    assert(!hle_call("user32.dll", "SetCursor", 1, &cursor));
    assert(hle_call("user32.dll", "SetCursor", 1, &cursor) == cursor);
    uint32_t display = ghalloc(424, 1); wr32(display, 424);
    assert(hle_call("user32.dll", "EnumDisplayDevicesA", 4, (uint32_t[]){0, 0, display, 0}));
    assert(!strcmp(GSTR(display+4), "\\\\.\\DISPLAY1") && rd32(display+164) == 5);
    assert(!rd8(display+168)); /* no fabricated physical PCI/PnP identity */
    assert(!hle_call("user32.dll", "EnumDisplayDevicesA", 4, (uint32_t[]){0, 1, display, 0}));
    wr32(display, 0);
    assert(!hle_call("user32.dll", "EnumDisplayDevicesA", 4, (uint32_t[]){0, 0, display, 0}));
    uint32_t cls = hle_wstr("FixtureClass"), caption = hle_wstr("Fixture Caption"), wc = ghalloc(40, 1);
    wr32(wc+4, fixture.base+0x1010); wr32(wc+16, fixture.base); wr32(wc+36, cls);
    uint32_t atom = hle_call("user32.dll", "RegisterClassW", 1, &wc); assert(atom);
    assert(!hle_call("user32.dll", "FindWindowW", 2, (uint32_t[]){cls, caption}));
    uint32_t win = hle_call("user32.dll", "CreateWindowExW", 12,
        (uint32_t[]){0, atom, caption, 0, 0, 0, 320, 240, 0, 0, fixture.base, 0}); assert(win);
    assert(hle_call("user32.dll", "FindWindowW", 2,
        (uint32_t[]){hle_wstr("fixtureclass"), hle_wstr("FIXTURE CAPTION")}) == win);
    assert(hle_call("user32.dll", "DestroyWindow", 1, &win));
    assert(!hle_call("user32.dll", "FindWindowW", 2, (uint32_t[]){cls, caption}));
    assert(hle_call("kernel32.dll", "GetUserDefaultLangID", 0, NULL) == 0x409);
    uint32_t masks = ghalloc(8, 1);
    assert(hle_call("kernel32.dll", "GetProcessAffinityMask", 3, (uint32_t[]){0xffffffff, masks, masks+4}));
    assert(rd32(masks) && rd32(masks) == rd32(masks+4));
    assert(!hle_call("d3d9.dll", "D3DPERF_GetStatus", 0, NULL));
    hle_call("d3d9.dll", "D3DPERF_SetOptions", 1, (uint32_t[]){1});
    assert(!hle_call("d3d9.dll", "D3DPERF_GetStatus", 0, NULL));
    assert(hle_call("kernel32.dll", "GetPriorityClass", 1, (uint32_t[]){0xffffffff}) == 0x20);
    assert(!hle_call("kernel32.dll", "SetPriorityClass", 2, (uint32_t[]){0xffffffff, 0x100}));
    assert(rd32(g_cpu.fs_base + 0x34) == 5);
    assert(hle_call("kernel32.dll", "GetPriorityClass", 1, (uint32_t[]){0xffffffff}) == 0x20);
    assert(hle_call("kernel32.dll", "SetPriorityClass", 2, (uint32_t[]){0xffffffff, 0x20}));
    assert(!hle_call("kernel32.dll", "GetPriorityClass", 1, (uint32_t[]){0}));
    assert(rd32(g_cpu.fs_base + 0x34) == 6);
    assert(!hle_call("kernel32.dll", "SetThreadPriority", 2, (uint32_t[]){0xfffffffe, 15}));
    assert(hle_call("kernel32.dll", "GetThreadPriority", 1, (uint32_t[]){0xfffffffe}) == 0);
    hle_set_cmdline("\"C:\\DeadSpace3\\deadspace3.exe\"");
    assert(!strcmp(GSTR(hle_call("kernel32.dll", "GetCommandLineA", 0, NULL)),
                   "\"C:\\DeadSpace3\\deadspace3.exe\""));
    assert(!hle_call("kernel32.dll", "HeapSetInformation", 4, (uint32_t[]){0, 1, 0, 0}));
    assert(rd32(g_cpu.fs_base + 0x34) == 50);
    assert(!hle_call("kernel32.dll", "HeapSetInformation", 4, (uint32_t[]){0, 99, 0, 0}));
    fs_check(root); diagnostic_check(); rmdir(root);
    puts("DS3 focused bootstrap checks: OK (not the renderer gate)"); return 0;
}
