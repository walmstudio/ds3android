/* v1.2.18: DS3 safe-fail import stubs and XInputGetCapabilities, called through the real HLE thunks.
 * Build: make build/test_ds3_stubs. Usage: build/test_ds3_stubs */
#include "rt.h"
#include "hle.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
static int g_fail, g_ok;
#define CHECK(cond, ...) do { if (cond) g_ok++; else { g_fail++; fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)
static uint32_t api(const char* dll, const char* name, int n, ...) {
    uint32_t t = hle_resolve_impl(dll, name), a[16]; va_list ap; va_start(ap, n);
    for (int i = 0; i < n; i++) a[i] = va_arg(ap, uint32_t);
    va_end(ap);
    if (!t) { fprintf(stderr, "FAIL: %s!%s not implemented\n", dll, name); g_fail++; return 0; }
    uint32_t sp = g_cpu.esp;
    for (int i = n - 1; i >= 0; i--) push32(&g_cpu, a[i]);
    push32(&g_cpu, 0xfffffff0);
    dispatch_call(&g_cpu, t);
    CHECK(g_cpu.esp == sp, "%s leaves esp changed (wrong nargs)", name);
    g_cpu.esp = sp;
    return g_cpu.eax;
}
static uint32_t W(const char* s) { uint32_t p = ghalloc(2 * strlen(s) + 2, 1); for (size_t k = 0; s[k]; k++) wr16(p + 2 * k, (uint8_t)s[k]); return p; }
static void put_rect(uint32_t p, int l, int t, int r, int b) { wr32(p, l); wr32(p + 4, t); wr32(p + 8, r); wr32(p + 12, b); }
int main(void) {
    rt_init();
    /* every import the exe needs and that used to be unbound must now resolve */
    static const char* const need[][2] = {
        {"ws2_32.dll","#1"},{"ws2_32.dll","#2"},{"ws2_32.dll","#5"},{"ws2_32.dll","#6"},{"ws2_32.dll","#7"},{"ws2_32.dll","#10"},{"ws2_32.dll","#13"},
        {"ws2_32.dll","#17"},{"ws2_32.dll","#18"},{"ws2_32.dll","#20"},{"ws2_32.dll","#21"},{"ws2_32.dll","#22"},{"ws2_32.dll","#57"},{"ws2_32.dll","#111"},
        {"ws2_32.dll","#116"},{"ws2_32.dll","WSAIoctl"},{"ws2_32.dll","WSARecv"},{"ws2_32.dll","WSARecvFrom"},{"ws2_32.dll","WSACreateEvent"},
        {"ws2_32.dll","WSAResetEvent"},{"ws2_32.dll","WSASetEvent"},{"ws2_32.dll","WSACloseEvent"},{"ws2_32.dll","WSAWaitForMultipleEvents"},
        {"ws2_32.dll","WSAGetOverlappedResult"},{"wsock32.dll","#11"},{"wsock32.dll","#101"},{"wsock32.dll","#151"},{"iphlpapi.dll","GetAdaptersAddresses"},
        {"advapi32.dll","RegEnumValueW"},{"psapi.dll","GetModuleBaseNameW"},{"psapi.dll","EnumProcesses"},{"kernel32.dll","CompareStringW"},
        {"kernel32.dll","lstrcmpiW"},{"kernel32.dll","PeekNamedPipe"},{"kernel32.dll","GetVolumePathNameW"},{"kernel32.dll","QueueUserAPC"},
        {"kernel32.dll","GetUserGeoID"},{"kernel32.dll","GetGeoInfoA"},{"user32.dll","IntersectRect"},{"user32.dll","GetKeyNameTextW"},
        {"user32.dll","RegisterDeviceNotificationA"},{"user32.dll","UnregisterDeviceNotification"},{"user32.dll","GetKeyboardLayoutNameA"},
        {"user32.dll","ValidateRect"},{"user32.dll","PostMessageA"},{"user32.dll","TrackMouseEvent"},{"user32.dll","UnhookWindowsHookEx"},
        {"user32.dll","CallNextHookEx"},{"winmm.dll","waveOutGetNumDevs"},{"winmm.dll","waveOutGetDevCapsA"},{"winmm.dll","waveInGetNumDevs"},
        {"winmm.dll","waveInGetDevCapsA"},{"winmm.dll","waveInOpen"},{"winmm.dll","waveInClose"},{"winmm.dll","waveInPrepareHeader"},
        {"winmm.dll","waveInUnprepareHeader"},{"winmm.dll","waveInAddBuffer"},{"winmm.dll","waveInStart"},{"winmm.dll","waveInStop"},
        {"winmm.dll","waveInReset"},{"winmm.dll","waveInMessage"},{"winmm.dll","waveOutMessage"},{"xinput1_3.dll","#4"} };
    for (unsigned i = 0; i < sizeof need / sizeof need[0]; i++) CHECK(hle_resolve_impl(need[i][0], need[i][1]), "%s!%s unbound", need[i][0], need[i][1]);
    printf("checked %zu expected import bindings\n", sizeof need / sizeof need[0]);

    uint32_t b = ghalloc(4096, 1), r, a = ghalloc(16, 1), d = ghalloc(16, 1), e = ghalloc(16, 1);
    /* CompareStringW: CSTR_LESS 1 / EQUAL 2 / GREATER 3, counted and NUL-terminated lengths, ignore-case flag */
    CHECK(api("kernel32.dll","CompareStringW",6,0x409u,0u,W("abc"),(uint32_t)-1,W("abd"),(uint32_t)-1)==1, "abc<abd");
    CHECK(api("kernel32.dll","CompareStringW",6,0x409u,0u,W("abc"),(uint32_t)-1,W("abc"),(uint32_t)-1)==2, "abc==abc");
    CHECK(api("kernel32.dll","CompareStringW",6,0x409u,0u,W("abd"),(uint32_t)-1,W("abc"),(uint32_t)-1)==3, "abd>abc");
    CHECK(api("kernel32.dll","CompareStringW",6,0x409u,0u,W("ABC"),(uint32_t)-1,W("abc"),(uint32_t)-1)!=2, "case-sensitive by default");
    CHECK(api("kernel32.dll","CompareStringW",6,0x409u,1u,W("ABC"),(uint32_t)-1,W("abc"),(uint32_t)-1)==2, "NORM_IGNORECASE");
    CHECK(api("kernel32.dll","CompareStringW",6,0x409u,0u,W("abcdef"),3u,W("abcxyz"),3u)==2, "counted length");
    CHECK(api("kernel32.dll","CompareStringW",6,0x409u,0u,W("ab"),(uint32_t)-1,W("abc"),(uint32_t)-1)==1, "shorter prefix is less");
    CHECK(api("kernel32.dll","CompareStringW",6,0x409u,1u,W("\xD0"),(uint32_t)-1,W("a"),(uint32_t)-1)!=0, "returns a CSTR value");
    CHECK((int32_t)api("kernel32.dll","lstrcmpiW",2,W("Dead Space"),W("DEAD SPACE"))==0, "lstrcmpiW equal");
    CHECK((int32_t)api("kernel32.dll","lstrcmpiW",2,W("a"),W("B"))<0, "lstrcmpiW order");
    /* IntersectRect */
    put_rect(a, 0, 0, 10, 10); put_rect(d, 5, 5, 20, 20);
    CHECK(api("user32.dll","IntersectRect",3,e,a,d)==1 && (int)rd32(e)==5 && (int)rd32(e+4)==5 && (int)rd32(e+8)==10 && (int)rd32(e+12)==10, "overlap");
    put_rect(d, 10, 0, 20, 10);
    CHECK(api("user32.dll","IntersectRect",3,e,a,d)==0 && !rd32(e) && !rd32(e+8), "touching edges = empty");
    /* GetKeyNameTextW: scan code 0x1E = A; 0x39 = Space; extended 0x48 = Up; buffer truncation; unknown scan code */
    r = api("user32.dll","GetKeyNameTextW",3,0x001E0000u,b,64u); CHECK(r==1 && rd16(b)=='A' && rd16(b+2)==0, "key A (%u)", r);
    r = api("user32.dll","GetKeyNameTextW",3,0x00390000u,b,64u); CHECK(r==5 && rd16(b)=='S', "key Space (%u)", r);
    r = api("user32.dll","GetKeyNameTextW",3,0x01480000u,b,64u); CHECK(r==2 && rd16(b)=='U' && rd16(b+2)=='p', "ext Up (%u)", r);
    r = api("user32.dll","GetKeyNameTextW",3,0x00390000u,b,3u); CHECK(r==2 && rd16(b+4)==0, "truncated to cap-1 (%u)", r);
    r = api("user32.dll","GetKeyNameTextW",3,0x00FE0000u,b,64u); CHECK(r==0 && rd16(b)==0, "unknown scan code is empty (%u)", r);
    CHECK(api("user32.dll","GetKeyNameTextW",3,0x001E0000u,b,0u)==0, "cap 0");
    /* misc */
    CHECK(api("user32.dll","GetKeyboardLayoutNameA",1,b)==1 && !strcmp((char*)M+b,"00000409"), "layout name");
    r = api("kernel32.dll","GetVolumePathNameW",3,W("C:\\x\\y"),b,8u); CHECK(r==1 && rd16(b)=='C' && rd16(b+2)==':' && rd16(b+4)=='\\', "volume path");
    CHECK(api("kernel32.dll","GetVolumePathNameW",3,W("C:\\x"),b,2u)==0, "volume path, small buffer");
    CHECK(api("kernel32.dll","GetUserGeoID",1,16u)==244, "geo id");
    CHECK(api("advapi32.dll","RegEnumValueW",8,0x80000001u,0u,b,b+64,0u,0u,0u,0u)==259, "RegEnumValueW end of list");
    CHECK(api("iphlpapi.dll","GetAdaptersAddresses",5,0u,0u,0u,0u,0u)==232, "no adapters");
    wr32(b, 99); CHECK(api("psapi.dll","EnumProcesses",3,b+64,16u,b)==1 && rd32(b)==0, "EnumProcesses cbNeeded=0");
    CHECK(api("kernel32.dll","QueueUserAPC",3,0u,0u,0u)==0, "QueueUserAPC reports failure, does not crash");
    CHECK(rd32(g_cpu.fs_base + 0x34)==50, "APC failure explains not supported");
    CHECK(api("user32.dll","RegisterDeviceNotificationA",3,0u,0u,0u)==0, "no fake notification handle");
    CHECK(api("user32.dll","TrackMouseEvent",1,0u)==0, "no unimplemented tracking success");
    /* network is unavailable but must fail cleanly */
    CHECK(api("ws2_32.dll","#111",0)==10050, "WSAGetLastError = WSAENETDOWN");
    CHECK(api("ws2_32.dll","#18",5,0u,0u,0u,0u,0u)==0xFFFFFFFFu, "select fails");
    CHECK(api("ws2_32.dll","WSAWaitForMultipleEvents",5,0u,0u,0u,0u,0u)==0xFFFFFFFFu, "WSA_WAIT_FAILED");
    CHECK(api("ws2_32.dll","WSAIoctl",9,0u,0u,0u,0u,0u,0u,0u,0u,0u)==0xFFFFFFFFu, "WSAIoctl 9 args");
    CHECK(!api("ws2_32.dll","WSASetEvent",1,0u), "no fake event success");
    CHECK(api("ws2_32.dll","#115",2,0x202u,b)==10091, "existing networking startup failure");
    r = api("wsock32.dll","#11",1,0x0100007fu); CHECK(r && !strcmp((char*)M + r, "127.0.0.1"), "inet_ntoa returns a string");
    wr32(b, 2); wr32(b+4, 10); wr32(b+8, 20);
    CHECK(api("wsock32.dll","#151",2,20u,b)==1 && api("wsock32.dll","#151",2,30u,b)==0, "bounded fd_set membership");
    /* winmm */
    CHECK(api("winmm.dll","waveInGetNumDevs",0)==0, "no capture device");
    CHECK(api("winmm.dll","waveOutGetNumDevs",0)==1, "existing wave output device retained");
    memset(M + b, 0xEE, 64);
    CHECK(api("winmm.dll","waveOutGetDevCapsA",3,0u,b,52u)==0 && !strcmp((char*)M+b+8,"DS3 PCM mixer") && rd16(b+44)==2, "existing output caps");
    CHECK(api("winmm.dll","waveOutGetDevCapsA",3,7u,b,52u)==2, "bad device id");
    CHECK(api("winmm.dll","waveInOpen",6,0u,0u,0u,0u,0u,0u)==6, "waveInOpen -> NODRIVER");
    CHECK(api("winmm.dll","waveInMessage",4,0u,0u,0u,0u)==6, "waveInMessage -> NODRIVER");
    CHECK(api("winmm.dll","waveOutMessage",4,0u,0u,0u,0u)==8, "existing waveOutMessage -> NOTSUPPORTED");
    /* XInputGetCapabilities follows the same connected state as XInputGetState */
    hle_xinput_connect(0);
    CHECK(api("xinput1_3.dll","#4",3,0u,0u,b)==1167, "not connected -> ERROR_DEVICE_NOT_CONNECTED");
    hle_xinput_set(0, 0, 0, 0, 0, 0, 0);   /* first update connects pad 0 (mode 2) */
    memset(M + b, 0, 32);
    wr8(b+20, 0xA5);
    CHECK(api("xinput1_3.dll","#4",3,0u,0u,b)==0 && M[b]==1 && M[b+1]==1 && rd16(b+4)==0xF3FF && M[b+6]==255, "connected caps (gamepad)");
    CHECK(rd8(b+20)==0xA5, "capability call writes precisely 20 bytes");
    CHECK(api("xinput1_3.dll","#4",3,0u,0u,0u)==87, "null capability output rejected");
    CHECK(api("xinput1_3.dll","XInputGetCapabilities",3,1u,0u,b)==1167, "user 1 not connected");
    printf("%d checks passed, %d failed\n", g_ok, g_fail);
    return g_fail != 0;
}
