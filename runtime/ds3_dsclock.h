#ifndef DS3_DSCLOCK_H
#define DS3_DSCLOCK_H

/* A frame may be mapped from the guest's modulo offset only in the
 * attested forward half. Keep the resulting absolute interval until Unlock. */
static int ds_ring_bind(uint64_t now, uint32_t offset, uint32_t count, uint64_t* first) {
    uint32_t ahead = (offset + DS45_RING - now % DS45_RING) % DS45_RING;
    if (!count || count > DS45_RING / 2 || ahead > DS45_RING / 2 - count) return 0;
    *first = now + ahead;
    return 1;
}

#endif
