/*
 * frank-genesis — C2 inter-processor sound link
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * link_proto.c — control-frame helpers shared by both halves.
 *
 * Ported from frank_core2/firmware/common/link_proto.c; the bulk
 * pattern generator and verifier that firmware used to characterise the
 * wire are dropped, since the emulator sends real data rather than test
 * patterns.
 */

#include "link_proto.h"

#include <string.h>

/* CRC-32 (IEEE, reflected) four bits at a time from a 16-entry table:
 * the bitwise loop ran ~4,000 cycles a 128-byte control frame, and every
 * stream request checks one (a tenth of the slave CPU core's link time
 * under an A1200). Same values. */
static const uint32_t crc_nibble[16] = {
    0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu,
    0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
    0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu,
    0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu,
};

uint32_t link_crc32(const void *data, uint32_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFu;

    for (uint32_t i = 0; i < len; i++) {
        crc ^= p[i];
        crc = (crc >> 4) ^ crc_nibble[crc & 15u];
        crc = (crc >> 4) ^ crc_nibble[crc & 15u];
    }
    return ~crc;
}

void link_frame_build(void *frame, uint16_t op, uint32_t seq,
                      uint32_t arg0, uint32_t arg1,
                      const void *payload, uint32_t payload_len) {
    uint8_t *f = (uint8_t *)frame;

    memset(f, 0, LINK_CTRL_BYTES);

    link_hdr_t *h = (link_hdr_t *)f;
    h->magic     = LINK_MAGIC;
    h->op        = op;
    h->proto_ver = LINK_PROTO_VER;
    h->seq       = seq;
    h->arg0      = arg0;
    h->arg1      = arg1;
    h->crc       = 0;

    if (payload && payload_len) {
        if (payload_len > LINK_PAYLOAD_BYTES) payload_len = LINK_PAYLOAD_BYTES;
        memcpy(f + sizeof(link_hdr_t), payload, payload_len);
    }

    h->crc = link_crc32(f, LINK_CTRL_BYTES);
}

int link_frame_check(const void *frame) {
    const uint8_t *f = (const uint8_t *)frame;
    const link_hdr_t *h = (const link_hdr_t *)f;

    if (h->magic != LINK_MAGIC)         return 0;
    if (h->proto_ver != LINK_PROTO_VER) return 0;

    /* Recompute over a copy with the crc field zeroed. The caller's
     * buffer is the live DMA landing area, so we must not mutate it;
     * 128 bytes of stack is cheaper than a resumable CRC. */
    uint32_t want = h->crc;

    uint8_t scratch[LINK_CTRL_BYTES];
    memcpy(scratch, f, LINK_CTRL_BYTES);
    ((link_hdr_t *)scratch)->crc = 0;

    return link_crc32(scratch, LINK_CTRL_BYTES) == want;
}
