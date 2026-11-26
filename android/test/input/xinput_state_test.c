/* Exercise the production HLE state/capabilities handlers with guest-layout memory. */
#include <assert.h>
#include "../../../runtime/hle_input.c"

static uint8_t arena[512];
uint8_t *M = arena;

int main(void) {
    CPU c = { .esp = 16 };
    unsigned out = 128;
    assert(setenv("ME1_XINPUT", "2", 1) == 0);
    wr32(c.esp + 8, out);
    xi_GetState(&c);
    assert(c.eax == 1167);

    hle_xinput_set(0x1189, 64, 255, -32768, 32767, 16384, -16384);
    xi_GetState(&c);
    assert(c.eax == 0 && rd16(out + 4) == 0x1189);
    assert(rd8(out + 6) == 64 && rd8(out + 7) == 255);
    assert((int16_t)rd16(out + 8) == -32768 && (int16_t)rd16(out + 10) == 32767);
    assert((int16_t)rd16(out + 12) == 16384 && (int16_t)rd16(out + 14) == -16384);
    wr32(c.esp + 8, 1);
    wr32(c.esp + 12, out);
    xi_GetCapabilities(&c);
    assert(c.eax == 0 && rd8(out) == 1 && rd8(out + 1) == 1);

    hle_xinput_connect(0);
    xi_GetCapabilities(&c);
    assert(c.eax == 1167);
    wr32(c.esp + 8, out);
    xi_GetState(&c);
    assert(c.eax == 1167 && hle_xinput_vibration() == 0);
    puts("HLE guest state, capabilities and disconnect: PASS");
}
