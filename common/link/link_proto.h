/*
 * frank-blood — inter-processor link protocol (control plane)
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Fixed-size control frames sequenced by the doorbells (link_session), used
 * for setup: HELLO, configuration, Kickstart / ADF uploads, link tests. The
 * steady-state video/audio stream (slave -> master) and input packets
 * (master -> slave) run on their own framing, defined in link_stream.h.
 */
#ifndef LINK_PROTO_H
#define LINK_PROTO_H

#include <stdint.h>

#define LINK_MAGIC        0x41474D41u   /* "AMGA" */
#define LINK_PROTO_VER    1u

/* Fixed control-frame size. Must be a multiple of 4 (PIO autopush is
 * 32-bit) and large enough for the biggest payload struct below. */
#define LINK_CTRL_BYTES   128u

/* ---- Opcodes ---- */
enum {
    LINK_OP_HELLO        = 0x0001,  /* M->S: are you there?               */
    LINK_OP_HELLO_ACK    = 0x0002,  /* S->M: payload link_node_info_t     */

    LINK_OP_PING         = 0x0040,  /* M->S: liveness / latency probe     */
    LINK_OP_PONG         = 0x0041,

    /* Link soak test. arg0 = bytes, arg1 = pattern seed.
     *   SOAK_M2S: the master sends the bulk; the ack carries arg0 = byte
     *             errors the slave counted, arg1 = 1 if every word matched.
     *   SOAK_S2M: the ack precedes a bulk from the slave, which the
     *             master checks against the same pattern. */
    LINK_OP_SOAK_M2S     = 0x0050,
    LINK_OP_SOAK_M2S_ACK = 0x0051,
    LINK_OP_SOAK_S2M     = 0x0052,
    LINK_OP_SOAK_S2M_ACK = 0x0053,
    /* arg0 = bulk clkdiv x 100 for both directions. */
    LINK_OP_SET_CLKDIV   = 0x0054,
    LINK_OP_SET_CLKDIV_ACK = 0x0055,

    /* Uploads into the slave's memory (Kickstart, extended ROM, disks).
     *   UPLOAD_BEGIN  arg0 = target (LINK_UP_*), arg1 = total bytes
     *   UPLOAD_CHUNK  arg0 = byte offset, arg1 = bytes (<= LINK_UPLOAD_CHUNK),
     *                 then the bulk from the master
     *   UPLOAD_END    arg0 = CRC-32 of the whole upload; the ack's arg0 is the
     *                 CRC the slave computed, arg1 = 1 if it matched
     * A running emulation is held at a frame boundary meanwhile. Every
     * request is answered by the same op | 1 (ACK). */
    LINK_OP_UPLOAD_BEGIN = 0x0070,
    LINK_OP_UPLOAD_CHUNK = 0x0072,
    LINK_OP_UPLOAD_END   = 0x0074,
    /* BOOT: arg0 = flags; (re)start the machine from the uploaded ROM. */
    LINK_OP_BOOT         = 0x0076,
    /* INSERT: arg0 = drive, arg1 = 0 eject / 1 insert the uploaded disk. */
    LINK_OP_INSERT       = 0x0078,
    /* Disk write-back (the stream's chunks carry LINK_STREAM_WORDS_DIRTY
     * while the slave has written tracks the master has not taken):
     *   DIRTY     arg0 = drive; the ack's arg0 = tracks, payload = bitmap of
     *             LINK_DISK_TRACKS bits (track = cylinder * 2 + side), now
     *             the master's to save
     *   DOWNLOAD  arg0 = LINK_UP_DF0/DF1, arg1 = track; ack, then the
     *             track's LINK_TRACK_BYTES as a bulk from the slave */
    LINK_OP_DIRTY        = 0x007A,
    /* STATE: arg0 = 0 save / 1 load, arg1 = slot; done at the next frame
     * end (the slave's console reports it). */
    LINK_OP_STATE        = 0x007E,
    /* Flux-image drives (INSERT arg1 = 2: the master sends their tracks):
     *   TRACKREQ   the ack's arg0 = drive or 0xFF, arg1 = track wanted
     *              (cylinder * 2 + side)
     *   TRACKDATA  arg0 = drive | track << 8, arg1 = bytes (MFM, multiple of
     *              4, <= LINK_UPLOAD_CHUNK), then the bulk from the master */
    LINK_OP_TRACKREQ     = 0x0080,
    LINK_OP_TRACKDATA    = 0x0082,
    LINK_OP_DOWNLOAD     = 0x007C,
    /* The IDE drive's hard-disk image (A1200 Gayle), on the master's card
     * (the stream's chunks carry LINK_STREAM_WORDS_HDF while the slave
     * wants something; BOOT arg1 = the image's 512-byte sectors):
     *   HDFREQ   the ack's arg0 = 0 nothing, 1 a block is wanted (arg1 =
     *            block, LINK_HDF_BLOCK bytes), 2 a written sector (arg1 =
     *            LBA, then its 512 bytes as a bulk from the slave)
     *   HDFDATA  arg0 = block, arg1 = bytes (LINK_HDF_BLOCK), then the bulk
     *            from the master */
    LINK_OP_HDFREQ       = 0x0084,
    LINK_OP_HDFDATA      = 0x0086,
    /* POKE: arg0 = Amiga address, arg1 = byte to write, or LINK_POKE_READ
     * to only read; the ack's arg0 = the byte there afterwards. Done
     * between the CPU's instructions (a game's variables, for testing). */
    LINK_OP_POKE         = 0x0088,
    /* Savestates through the master (its SD card; an A1200's 4 MB do not
     * fit the slave's PSRAM): STATE arg0 = 2 freezes the machine at the
     * next frame end, 3 resumes it, 4 resumes it after its state was
     * written back. While frozen:
     *   STATEINFO  the ack's payload: link_state_info_t
     *   STATEREAD  arg0 = region << 28 | offset, arg1 = bytes (<=
     *              LINK_UPLOAD_CHUNK, multiple of 4, within one 64 KB
     *              bank); ack (arg0 = ok), then the bytes as a bulk
     *   STATEWRITE the same, the bytes as a bulk from the master first */
    LINK_OP_STATEINFO    = 0x008A,
    LINK_OP_STATEREAD    = 0x008C,
    LINK_OP_STATEWRITE   = 0x008E,
};
enum { LINK_STATE_DATA, LINK_STATE_BSS, LINK_STATE_CHIP, LINK_STATE_SLOW,
       LINK_STATE_FAST, LINK_STATE_REGIONS };
typedef struct {
    uint32_t magic;         /* LINK_STATE_MAGIC */
    uint32_t frozen;        /* the machine is stopped for the transfer */
    uint32_t frame;
    uint32_t data_start, bss_start;   /* the firmware's layout */
    uint32_t len[LINK_STATE_REGIONS]; /* bytes of each region */
} link_state_info_t;
#define LINK_STATE_MAGIC 0x41535632u  /* "ASV2" */
#define LINK_POKE_READ 0x100u
#define LINK_HDF_BLOCK 4096u
#define LINK_DISK_TRACKS 160u
#define LINK_TRACK_BYTES (11u * 512u)

enum {
    LINK_UP_ROM = 1,      /* Kickstart, 256 or 512 KB */
    LINK_UP_EXT = 2,      /* extended ROM at 0xE00000, 512 KB */
    LINK_UP_DF0 = 3,      /* raw ADF for DF0, 901120 bytes */
    LINK_UP_DF1 = 4,      /* raw ADF for DF1 (also connects the drive) */
    /* BOOT arg0 flags */
    LINK_BOOT_DF1 = 1,       /* DF1 connected                          */
    LINK_BOOT_DF0_DISK = 2,  /* DF0 has the uploaded disk               */
    LINK_BOOT_DF1_DISK = 4,  /* DF1 has the uploaded disk               */
    LINK_BOOT_DF0_EXT = 0x1000, /* DF0 / DF1 hold flux images (tracks from */
    LINK_BOOT_DF1_EXT = 0x2000, /* the master, TRACKREQ/TRACKDATA)         */
    LINK_BOOT_NTSC = 0x4000,    /* an NTSC machine (60 Hz, 263 lines)       */
    LINK_BOOT_ROWS256 = 0x8000, /* the display shows all 256 image rows     */
    LINK_BOOT_FLOPPY_REAL = 0x10000, /* disks at real speed (LINK_EV_FLOPPY) */
    LINK_BOOT_A1200 = 0x20000,  /* an A1200: AGA, 68EC020 at 14 MHz        */
    /* bits 8-9: chip RAM (0 512 KB, 1 1 MB, 2 2 MB); bits 10-11: slow RAM
     * at 0xC00000 (0 512 KB, 1 none, 2 1 MB). 0 is an A500's 512 + 512. */
    LINK_BOOT_CHIP_SHIFT = 8,
    LINK_BOOT_SLOW_SHIFT = 10,
};
#define LINK_UPLOAD_CHUNK (16u * 1024u)

/* ---- Frame header (24 bytes), followed by payload, zero-padded to
 *      LINK_CTRL_BYTES. ---- */
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t op;
    uint16_t proto_ver;
    uint32_t seq;
    uint32_t arg0;
    uint32_t arg1;
    uint32_t crc;      /* CRC-32 of the whole frame with crc treated as 0 */
} link_hdr_t;

#define LINK_PAYLOAD_BYTES (LINK_CTRL_BYTES - sizeof(link_hdr_t))

/* HELLO_ACK payload: lets the master refuse to run against a slave built
 * for a different clock (the link would drop bytes one way) or protocol. */
typedef struct __attribute__((packed)) {
    uint32_t sys_clk_hz;
    uint32_t proto_ver;
    uint32_t psram_bytes;     /* 0 when the PSRAM self-test failed */
    uint32_t uptime_ms;
    char     build[32];
} link_node_info_t;

/* The PIO FIFOs are 32 bits wide and autopull/autopush are word-sized,
 * so every bulk transfer must be a multiple of 4 bytes (and the buffer
 * aligned). */
#define LINK_ALIGN4(n) (((n) + 3u) & ~3u)

uint32_t link_crc32(const void *data, uint32_t len);

/* Build a complete control frame into `frame` (LINK_CTRL_BYTES, 4-byte
 * aligned). Copies `payload_len` bytes of payload and fills in the CRC. */
void link_frame_build(void *frame, uint16_t op, uint32_t seq,
                      uint32_t arg0, uint32_t arg1,
                      const void *payload, uint32_t payload_len);

/* 1 if magic, version and CRC are all good. */
int link_frame_check(const void *frame);

/* Deterministic soak pattern: word i of a buffer seeded with `seed`. */
static inline uint32_t link_soak_word(uint32_t seed, uint32_t i) {
    uint32_t x = seed ^ (i * 0x9E3779B9u);
    x ^= x >> 16; x *= 0x7FEB352Du;
    x ^= x >> 15; x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

#endif /* LINK_PROTO_H */
