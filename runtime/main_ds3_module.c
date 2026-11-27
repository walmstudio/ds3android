#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "rt.h"
#include "selfcheck.h"

extern const uint32_t ds3_base;
extern const uint32_t ds3_entry;
extern const uint32_t ds3_image_size;
extern const FuncEntry ds3_funcs[];
extern const unsigned ds3_nfuncs;

typedef struct {
    uint32_t state[8];
    uint64_t bits;
    unsigned used;
    uint8_t block[64];
} Sha256;

static uint32_t rotr(uint32_t value, unsigned count) {
    return (value >> count) | (value << (32 - count));
}

static void sha256_block(Sha256 *ctx, const uint8_t block[64]) {
    static const uint32_t k[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2,
    };
    uint32_t w[64], a, b, c, d, e, f, g, h;
    for (unsigned i = 0; i < 16; i++)
        w[i] = ((uint32_t)block[i*4] << 24) | ((uint32_t)block[i*4+1] << 16) |
               ((uint32_t)block[i*4+2] << 8) | block[i*4+3];
    for (unsigned i = 16; i < 64; i++) {
        uint32_t s0 = rotr(w[i-15],7) ^ rotr(w[i-15],18) ^ (w[i-15] >> 3);
        uint32_t s1 = rotr(w[i-2],17) ^ rotr(w[i-2],19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    a=ctx->state[0]; b=ctx->state[1]; c=ctx->state[2]; d=ctx->state[3];
    e=ctx->state[4]; f=ctx->state[5]; g=ctx->state[6]; h=ctx->state[7];
    for (unsigned i = 0; i < 64; i++) {
        uint32_t s1=rotr(e,6)^rotr(e,11)^rotr(e,25), ch=(e&f)^(~e&g);
        uint32_t t1=h+s1+ch+k[i]+w[i], s0=rotr(a,2)^rotr(a,13)^rotr(a,22);
        uint32_t maj=(a&b)^(a&c)^(b&c), t2=s0+maj;
        h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    ctx->state[0]+=a; ctx->state[1]+=b; ctx->state[2]+=c; ctx->state[3]+=d;
    ctx->state[4]+=e; ctx->state[5]+=f; ctx->state[6]+=g; ctx->state[7]+=h;
}

static int image_sha256(const char *path, uint8_t digest[32]) {
    Sha256 ctx = {{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                   0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19},0,0,{0}};
    FILE *stream = fopen(path, "rb");
    if (!stream) return 0;
    for (;;) {
        size_t count = fread(ctx.block + ctx.used, 1, 64 - ctx.used, stream);
        ctx.used += (unsigned)count;
        ctx.bits += (uint64_t)count * 8;
        if (ctx.used == 64) { sha256_block(&ctx, ctx.block); ctx.used = 0; }
        if (count == 0) break;
    }
    if (ferror(stream)) { fclose(stream); return 0; }
    fclose(stream);
    ctx.block[ctx.used++] = 0x80;
    if (ctx.used > 56) { memset(ctx.block + ctx.used, 0, 64 - ctx.used); sha256_block(&ctx, ctx.block); ctx.used = 0; }
    memset(ctx.block + ctx.used, 0, 56 - ctx.used);
    for (unsigned i = 0; i < 8; i++) ctx.block[63-i] = (uint8_t)(ctx.bits >> (i*8));
    sha256_block(&ctx, ctx.block);
    for (unsigned i = 0; i < 8; i++) for (unsigned j = 0; j < 4; j++)
        digest[i*4+j] = (uint8_t)(ctx.state[i] >> (24-j*8));
    return 1;
}

static int self_check(void) {
    struct stat image;
    static const uint8_t expected_digest[32] = DS3_EXPECTED_IMAGE_SHA256;
    uint8_t actual_digest[32];
    int entry_mapped = 0;
    const char *path = getenv("DS3_IMAGE");
    if (!path || !*path) path = "out/exe/image.bin";
    for (unsigned i = 0; i < ds3_nfuncs; i++)
        if (ds3_funcs[i].va == ds3_entry && ds3_funcs[i].fn != NULL) entry_mapped = 1;
    if (stat(path, &image) != 0) {
        perror(path);
        return 1;
    }
    if ((uint64_t)image.st_size != ds3_image_size || ds3_base != DS3_EXPECTED_BASE ||
        ds3_entry != ds3_base + DS3_EXPECTED_ENTRY_RVA || ds3_entry < ds3_base ||
        ds3_entry >= ds3_base + ds3_image_size ||
        (DS3_RELOCATION_COUNT && DS3_RELOCATION_MAX_RVA + sizeof(uint32_t) > ds3_image_size) ||
        !entry_mapped || !image_sha256(path, actual_digest) ||
        memcmp(actual_digest, expected_digest, sizeof actual_digest) != 0) {
        fputs("DS3 module self-check: FAILED\n", stderr);
        return 1;
    }
    puts("DS3 module self-check: OK");
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--self-check") == 0)
        return self_check();
    fputs("usage: test_ds3_module --self-check\n", stderr);
    return 2;
}
