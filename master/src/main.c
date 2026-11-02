/*
 * frank-blood — master (RP2350B, U3): Blood on one chip.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Core 0 runs the game, core 1 the HDMI scanout (and later the audio mix).
 * The console on UART0 (J2) takes commands while the game runs: type `help`.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/watchdog.h"
#include "hardware/regs/sysinfo.h"
#include "hardware/regs/addressmap.h"
#include "hardware/sync.h"

#include "clocks.h"
#include "console.h"
#include "display.h"
#include "shell.h"
#include "ff.h"
#include "frank_game.h"
#include "psram_init.h"
#include "usb/usbhid.h"
#include "audio/audio.h"
#include "driver_frank.h"

/* PSRAM chip select: GPIO 47 on an RP2350B (Core 2 master, M2 with a B),
 * GPIO 8 on an RP2350A (M2 boards with the A package). */
static uint psram_cs_pin(void) {
    const uint32_t package_sel = *(io_ro_32 *)(SYSINFO_BASE + SYSINFO_PACKAGE_SEL_OFFSET);
    return (package_sel & 1) ? 8u : 47u;
}

/* Clocks and PSRAM come up before C++ constructors and main(): the game's
 * zero-initialised data lives in PSRAM (memmap_blood.ld). A plain
 * .preinit_array entry runs after all of the SDK's initialisers (it needs
 * the coprocessors on: the clock code uses floating point), but entries
 * there are per-core too: core 1 runs it again when it starts, which must
 * not re-initialise PSRAM under core 0's feet or clear the game's data. */
extern char __psram_bss_start[], __psram_bss_end[];
extern char __sram_hot_start[], __sram_hot_end[];
static bool clocks_ok;

static void frank_preinit(void) {
    if (get_core_num() != 0) return;
    clocks_ok = frank_clocks_init();
    psram_init_with_freq(psram_cs_pin(), FRANK_PSRAM_MAX_MHZ);
    memset(__psram_bss_start, 0, (size_t)(__psram_bss_end - __psram_bss_start));
    memset(__sram_hot_start, 0, (size_t)(__sram_hot_end - __sram_hot_start));
}
__attribute__((section(".preinit_array"), used))
static void (*const frank_preinit_entry)(void) = frank_preinit;

static FATFS fs;

static bool sd_mounted;

static bool sd_mount(void) {
    f_unmount("0:");
    /* A card caught mid-transfer by a reset can miss the first
     * initialisation: try a few times. */
    FRESULT r = FR_NOT_READY;
    for (int i = 0; i < 5 && r != FR_OK; i++) {
        if (i) sleep_ms(100);
        r = f_mount(&fs, "0:", 1);
    }
    sd_mounted = r == FR_OK;
    if (!sd_mounted) printf("sd: no microSD (%d)\n", r);
    return sd_mounted;
}

/* ---- SRAM pool for hot game tables (frank_game.h) ---- */
#define SRAM_POOL_BYTES (24u * 1024u)
static uint8_t sram_pool[SRAM_POOL_BYTES] __attribute__((aligned(8)));
static uint32_t sram_pool_used;

void *frank_sram_alloc(uint32_t bytes) {
    bytes = (bytes + 7u) & ~7u;
    if (sram_pool_used + bytes > SRAM_POOL_BYTES) return NULL;
    void *p = sram_pool + sram_pool_used;
    sram_pool_used += bytes;
    return p;
}

/* ---- Frame statistics and checks ---- */
static volatile uint32_t frames_shown;
static bool crc_on;

static uint32_t crc32_buf(const uint8_t *p, uint32_t n) {
    static uint32_t table[256];
    if (!table[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & -(c & 1u));
            table[i] = c;
        }
    uint32_t crc = 0xffffffffu;
    while (n--) crc = table[(crc ^ *p++) & 0xff] ^ (crc >> 8);
    return ~crc;
}

void frank_frame_shown(const uint8_t *frame) {
    if (crc_on)
        printf("frame %lu crc %08lx\n", (unsigned long)frames_shown,
               (unsigned long)crc32_buf(frame, DISPLAY_W * DISPLAY_H));
    frames_shown++;
    if (frank_fixed_ticks > 0) frank_timer_advance(frank_fixed_ticks);
}

/* /blood/frank.cfg: board options, `key=value` lines.
 *   demomenu=0   attract-mode demos without the main menu over them (benchmarks)
 *   ticks=4      deterministic: the game clock advances 4/120 s per shown frame,
 *                so every run renders the same frames (benchmarks, CRC checks)
 *   mix=3        audio mixing: bit 0 effects, bit 1 music (benchmarks)
 *   crc=1        print every shown frame's CRC-32 from boot (compare with the host) */
int frank_demo_menu = 1;

static void load_frank_cfg(void) {
    FIL f;
    char line[64];
    if (f_open(&f, "/blood/frank.cfg", FA_READ) != FR_OK) return;
    while (f_gets(line, sizeof(line), &f)) {
        int v;
        if (sscanf(line, "demomenu=%d", &v) == 1) frank_demo_menu = v;
        if (sscanf(line, "ticks=%d", &v) == 1) frank_fixed_ticks = v;
        if (sscanf(line, "crc=%d", &v) == 1) crc_on = v != 0;
        if (sscanf(line, "mix=%d", &v) == 1) { extern volatile uint32_t frank_mix_mask; frank_mix_mask = (uint32_t)v; }
    }
    f_close(&f);
    printf("frank.cfg: demomenu=%d ticks=%d\n", frank_demo_menu, frank_fixed_ticks);
}

/* Demo benchmark: average fps of each demo as it ends (demo.cpp marks). */
static uint32_t demo_frames0, demo_io0;
static uint64_t demo_t0;
extern volatile uint32_t frank_io_us;

void frank_demo_mark(int start, const char *level) {
    const uint64_t now = time_us_64();
    if (start) {
        demo_frames0 = frames_shown;
        demo_t0 = now;
        demo_io0 = frank_io_us;
        printf("demo %s: start\n", level);
        return;
    }
    const uint32_t n = frames_shown - demo_frames0;
    const uint64_t us = now - demo_t0;
    printf("demo %s: %lu frames in %lu.%01lu s = %lu.%01lu fps (microSD %lu ms)\n", level, (unsigned long)n,
           (unsigned long)(us / 1000000u), (unsigned long)(us / 100000u % 10u),
           (unsigned long)(us ? (uint64_t)n * 1000000u / us : 0),
           (unsigned long)(us ? (uint64_t)n * 10000000u / us % 10u : 0),
           (unsigned long)((frank_io_us - demo_io0) / 1000u));
}

/* ---- Console ---- */
static struct { int code; uint64_t up_at; } tapped[8];

static void cmd_key(int argc, char **argv) {
    if (argc < 2) { printf("key <scancode hex> [down|up|tap]\n"); return; }
    const int code = (int)strtol(argv[1], NULL, 16);
    const char *how = argc > 2 ? argv[2] : "tap";
    if (!strcmp(how, "down")) frank_key_event(code, 1);
    else if (!strcmp(how, "up")) frank_key_event(code, 0);
    else {
        frank_key_event(code, 1);
        for (int i = 0; i < 8; i++)
            if (!tapped[i].code) { tapped[i].code = code; tapped[i].up_at = time_us_64() + 100000; break; }
    }
}

/* Rates over the last 2 s, updated by frank_service(). */
static uint32_t game_fps_x10, hdmi_fps;
static bool stat_on;

static void cmd_fps(int argc, char **argv) {
    printf("fps: game %lu.%lu, hdmi %lu\n", (unsigned long)(game_fps_x10 / 10),
           (unsigned long)(game_fps_x10 % 10), (unsigned long)hdmi_fps);
}

static void cmd_stat(int argc, char **argv) {
    stat_on = !(argc > 1 && !strcmp(argv[1], "off"));
}

static void cmd_crc(int argc, char **argv) {
    crc_on = !(argc > 1 && !strcmp(argv[1], "off"));
}

static void cmd_ticks(int argc, char **argv) {
    frank_fixed_ticks = argc > 1 ? atoi(argv[1]) : 0;
    printf("timer: %s\n", frank_fixed_ticks ? "fixed ticks per frame" : "real time");
}

extern uint32_t frank_heap_used(void);

static void cmd_mem(int argc, char **argv) {
    extern char __bss_end__[], __StackLimit[];
    printf("psram: bss %lu KB, heap %lu KB; sram free %lu KB\n",
           (unsigned long)((__psram_bss_end - __psram_bss_start) >> 10),
           (unsigned long)(frank_heap_used() >> 10),
           (unsigned long)((__StackLimit - __bss_end__) >> 10));
}

static void cmd_ls(int argc, char **argv) {
    if (!sd_mounted && !sd_mount()) return;
    DIR dir;
    FILINFO fi;
    const char *path = argc > 1 ? argv[1] : ".";
    if (f_opendir(&dir, path) != FR_OK) { printf("ls: cannot open %s\n", path); return; }
    while (f_readdir(&dir, &fi) == FR_OK && fi.fname[0])
        printf("%10lu %s%s\n", (unsigned long)fi.fsize, fi.fname, (fi.fattrib & AM_DIR) ? "/" : "");
    f_closedir(&dir);
}

/* sdput <bytes> <path>: save what tools/sd_put.sh staged over SWD at the
 * top of PSRAM (read uncached: the debugger wrote around the cache). */
#define SD_STAGE ((const uint8_t *)0x157F0000u)
#define SD_STAGE_MAX (64u * 1024u)

static void cmd_sdput(int argc, char **argv) {
    if (argc < 3) { printf("sdput <bytes> <path>\n"); return; }
    const uint32_t n = (uint32_t)strtoul(argv[1], NULL, 0);
    if (n > SD_STAGE_MAX) { printf("ERR sdput: at most %u bytes\n", SD_STAGE_MAX); return; }
    FIL f;
    UINT put = 0;
    if (f_open(&f, argv[2], FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) { printf("ERR sdput: cannot create %s\n", argv[2]); return; }
    const FRESULT r = f_write(&f, SD_STAGE, n, &put);
    f_close(&f);
    printf("sdput: %s %lu bytes crc %08lx%s\n", argv[2], (unsigned long)put,
           (unsigned long)crc32_buf(SD_STAGE, n), (r == FR_OK && put == n) ? "" : " WRITE FAILED");
}

static void cmd_snd(int argc, char **argv) {
    extern volatile uint32_t opl_frank_ticks, opl_frank_writes, opl_frank_frames;
    extern volatile uint32_t frank_pcm_wait_us, frank_pcm_locks;
    printf("mixer lock: %lu locks by the game, %lu us waiting\n", (unsigned long)frank_pcm_locks,
           (unsigned long)frank_pcm_wait_us);
    printf("music: %lu ticks, %lu OPL writes, %lu frames; audio rate %lu, starved %lu, queued %lu\n",
           (unsigned long)opl_frank_ticks, (unsigned long)opl_frank_writes,
           (unsigned long)opl_frank_frames, (unsigned long)audio_get_rate(),
           (unsigned long)audio_starved, (unsigned long)audio_queued());
}

static void cmd_mix(int argc, char **argv) {
    extern volatile uint32_t frank_mix_mask;
    if (argc > 1) frank_mix_mask = (uint32_t)atoi(argv[1]);
    printf("mix: effects %s, music %s\n", (frank_mix_mask & 1) ? "on" : "off",
           (frank_mix_mask & 2) ? "on" : "off");
}

/* sdcrc <path>: read a whole file, print its CRC-32 and the speed. */
static void cmd_sdcrc(int argc, char **argv) {
    if (argc < 2) { printf("sdcrc <path>\n"); return; }
    FIL f;
    if (f_open(&f, argv[1], FA_READ) != FR_OK) { printf("ERR sdcrc: cannot open %s\n", argv[1]); return; }
    static uint32_t table[256];
    if (!table[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & -(c & 1u));
            table[i] = c;
        }
    static uint8_t buf[8192];
    uint32_t crc = 0xffffffffu, total = 0;
    UINT got;
    const uint64_t t0 = time_us_64();
    while (f_read(&f, buf, sizeof(buf), &got) == FR_OK && got) {
        for (UINT i = 0; i < got; i++) crc = table[(crc ^ buf[i]) & 0xff] ^ (crc >> 8);
        total += got;
    }
    const uint64_t us = time_us_64() - t0;
    f_close(&f);
    printf("sdcrc: %s %lu bytes crc %08lx, %lu KB/s\n", argv[1], (unsigned long)total,
           (unsigned long)~crc, (unsigned long)(us ? (uint64_t)total * 1000u / us : 0));
}

/* flashcrc <bytes>: CRC-32 of the firmware as read through XIP (checks the
 * flash clock: compare with the .bin's CRC). */
static void cmd_flashcrc(int argc, char **argv) {
    const uint32_t n = argc > 1 ? (uint32_t)strtoul(argv[1], NULL, 0) : 0;
    const uint8_t *p = (const uint8_t *)0x10000000u;
    const uint32_t t0 = time_us_32();
    printf("flashcrc: %lu bytes crc %08lx (%lu us)\n", (unsigned long)n,
           (unsigned long)crc32_buf(p, n), (unsigned long)(time_us_32() - t0));
}

static void cmd_io(int argc, char **argv) {
    extern void frank_io_report(void);
    frank_io_report();
}

static void cmd_tone(int argc, char **argv) {
    const uint32_t hz = argc > 1 ? (uint32_t)atoi(argv[1]) : 1000u;
    const uint32_t ms = argc > 2 ? (uint32_t)atoi(argv[2]) * 1000u : 3000u;
    driver_frank_tone(hz, ms);
    printf("tone %lu Hz for %lu ms (replaces the game's sound)\n", (unsigned long)hz, (unsigned long)ms);
}

static void cmd_usb(int argc, char **argv) {
    printf("usb: keyboard %s, mouse %s, gamepad %s\n",
           usbhid_keyboard_connected() ? "yes" : "no",
           usbhid_mouse_connected() ? "yes" : "no",
           usbhid_gamepad_connected() ? "yes" : "no");
}

/* Every software restart goes through here: the PSRAM back to SPI mode
 * first (left in QPI it hangs the boot ROM), core 1 stopped. */
static void __attribute__((noreturn)) frank_reboot(void) {
    printf("rebooting\n");
    sleep_ms(50);
    multicore_reset_core1();
    save_and_disable_interrupts();
    psram_reset_to_spi();
    watchdog_reboot(0, 0, 0);
    for (;;) tight_loop_contents();
}

static void cmd_reboot(int argc, char **argv) {
    frank_reboot();
}

static void cmd_errtest(int argc, char **argv);

static const console_cmd_t cmds[] = {
    { "key",    cmd_key,    "key <scancode hex> [down|up|tap]: Build scancode (c8 up, 1d ctrl, 39 space, 1c enter, 01 esc)" },
    { "fps",    cmd_fps,    "game and HDMI frames per second (last 2 s)" },
    { "stat",   cmd_stat,   "stat [off]: print fps and memory every 2 s" },
    { "crc",    cmd_crc,    "crc [off]: print each shown frame's CRC-32" },
    { "ticks",  cmd_ticks,  "ticks <n>: timer advances n/120 s per frame (0 = real time)" },
    { "mem",    cmd_mem,    "memory use" },
    { "ls",     cmd_ls,     "ls [path]: list a directory on the microSD" },
    { "usb",    cmd_usb,    "what the USB host sees" },
    { "snd",    cmd_snd,    "music and audio counters" },
    { "mix",    cmd_mix,    "mix <0-3>: bench, bit 0 effects, bit 1 music" },
    { "sdcrc",  cmd_sdcrc,  "sdcrc <path>: CRC-32 and read speed of a file on the microSD" },
    { "flashcrc", cmd_flashcrc, "flashcrc <bytes>: CRC-32 of the firmware read back from flash" },
    { "io",     cmd_io,     "microSD reads by file since boot" },
    { "errtest", cmd_errtest, "errtest sd|data|game: show an error screen, restart after 8 s" },
    { "tone",   cmd_tone,   "tone [Hz] [s]: a test sine on the DAC (default 1000 Hz, 3 s)" },
    { "sdput",  cmd_sdput,  "sdput <bytes> <path>: save the file tools/sd_put.sh staged (64 KB max)" },
    { "reboot", cmd_reboot, "restart the master" },
    { NULL, NULL, NULL },
};

/* Called by the game from handleevents(): console commands and key taps. */
void frank_service(void) {
    console_set_commands(cmds);
    console_service();
    const uint64_t now = time_us_64();
    static uint64_t last;
    static uint32_t last_frames, last_hdmi;
    if (now - last >= 2000000) {
        const uint32_t f = frames_shown, h = display_frames();
        game_fps_x10 = (uint32_t)((uint64_t)(f - last_frames) * 10000000u / (now - last));
        hdmi_fps = (uint32_t)((uint64_t)(h - last_hdmi) * 1000000u / (now - last));
        last = now;
        last_frames = f;
        last_hdmi = h;
        if (stat_on) {
            printf("stat: ");
            cmd_fps(0, NULL);
        }
    }
    for (int i = 0; i < 8; i++)
        if (tapped[i].code && now >= tapped[i].up_at) {
            frank_key_event(tapped[i].code, 0);
            tapped[i].code = 0;
        }
}

/* Core 1, after the scanout started: the audio DMA interrupt belongs there
 * too, next to the game's mixer (driver_frank.c). */
static void core1_audio_init(void) {
    audio_init();
}

/* ---- Boot: SD card, game data, errors ---- */

/* What the game needs in /blood (NBlood's list for Blood 1.21). */
static const char *const required_files[] = {
    "BLOOD.INI", "BLOOD.RFF", "GUI.RFF", "SOUNDS.RFF", "SURFACE.DAT", "VOXEL.DAT",
    "TILES000.ART", "TILES001.ART", "TILES002.ART", "TILES003.ART", "TILES004.ART",
    "TILES005.ART", "TILES006.ART", "TILES007.ART", "TILES008.ART", "TILES009.ART",
    "TILES010.ART", "TILES011.ART", "TILES012.ART", "TILES013.ART", "TILES014.ART",
    "TILES015.ART", "TILES016.ART", "TILES017.ART",
};
#define N_REQUIRED (sizeof(required_files) / sizeof(required_files[0]))

/* Missing files in /blood, listed into `out` (NULL: just count). */
static int missing_files(char *out, size_t out_size) {
    int n = 0;
    size_t len = 0;
    if (out && out_size) out[0] = 0;
    for (unsigned i = 0; i < N_REQUIRED; i++) {
        char path[32];
        FILINFO fi;
        snprintf(path, sizeof(path), "/blood/%s", required_files[i]);
        if (f_stat(path, &fi) == FR_OK && fi.fsize > 0) continue;
        n++;
        if (out && len + 16 < out_size)
            len += (size_t)snprintf(out + len, out_size - len, "%s ", required_files[i]);
    }
    return n;
}

/* Waiting on an error screen: USB, the console and a key. */
static bool any_key(void) {
    uint8_t code;
    int down;
    usbhid_task();
    console_set_commands(cmds);
    console_service();
    while (usbhid_get_key_action(&code, &down))
        if (down) return true;
    return false;
}

static void wait_for_sd(void) {
    if (sd_mount()) return;
    shell_error("NO SD CARD", "Waiting for the card...");
    shell_printf("Insert a FAT32 or exFAT microSD card with the Blood 1.21 data in:\n");
    shell_highlight("  /blood/BLOOD.RFF, SOUNDS.RFF, GUI.RFF,\n  BLOOD.INI, TILES000-017.ART, ...");
    while (!sd_mount()) {
        const uint64_t until = time_us_64() + 1000000;
        while (time_us_64() < until) any_key();
    }
    shell_welcome();
    shell_printf("SD card mounted.\n");
}

static void wait_for_data(void) {
    static char list[400];
    int n = missing_files(list, sizeof(list));
    if (!n) return;
    shell_error("NO GAME DATA", "Waiting for the files...");
    shell_printf("Copy the Blood 1.21 / One Unit Whole Blood data to /blood on the card. Missing:\n");
    shell_highlight("%s\n", list);
    /* The card may be swapped meanwhile: mount again each time. */
    while (n) {
        const uint64_t until = time_us_64() + 2000000;
        while (time_us_64() < until) any_key();
        n = sd_mount() ? missing_files(NULL, 0) : (int)N_REQUIRED;
    }
    shell_welcome();
    shell_printf("Game data found.\n");
}

/* A fatal game error (ThrowError): its message on the error screen until a
 * key, then a restart. */
void frank_fatal(const char *msg) {
    shell_error("GAME ERROR", "Press any key to restart");
    shell_errorf("%s\n", msg);
    while (!any_key()) tight_loop_contents();
    frank_reboot();
}

/* exit(): the game's Quit, or a panic. Restart (a panic shows a screen). */
void __attribute__((noreturn)) _exit(int status) {
    if (status != 0) {
        shell_error("SYSTEM ERROR", "Press any key to restart");
        shell_errorf("The firmware stopped (status %d). See the UART console.\n", status);
        while (!any_key()) tight_loop_contents();
    }
    frank_reboot();
}

/* errtest sd|data|game: show an error screen (bench: screenshots), then
 * restart after 8 s. */
static void cmd_errtest(int argc, char **argv) {
    const char *what = argc > 1 ? argv[1] : "game";
    if (!strcmp(what, "sd")) {
        shell_error("NO SD CARD", "Waiting for the card...");
        shell_printf("Insert a FAT32 or exFAT microSD card with the Blood 1.21 data in:\n");
        shell_highlight("  /blood/BLOOD.RFF, SOUNDS.RFF, GUI.RFF,\n  BLOOD.INI, TILES000-017.ART, ...");
    } else if (!strcmp(what, "data")) {
        shell_error("NO GAME DATA", "Waiting for the files...");
        shell_printf("Copy the Blood 1.21 / One Unit Whole Blood data to /blood on the card. Missing:\n");
        shell_highlight("SOUNDS.RFF TILES016.ART TILES017.ART\n");
    } else {
        shell_error("GAME ERROR", "Press any key to restart");
        shell_errorf("resource.cpp(778): Out of memory!\n");
    }
    const uint64_t until = time_us_64() + 8000000;
    while (time_us_64() < until) tight_loop_contents();
    frank_reboot();
}

int main(void) {
    stdio_init_all();
    printf("\n\nfrank-blood " FRANK_BLOOD_VERSION " on " FRANK_BOARD_NAME " (%s %s)\n", __DATE__, __TIME__);
    if (!clocks_ok) printf("WARNING: clock setup failed\n");
    frank_clocks_report();

    if (!display_init(core1_audio_init, driver_frank_service)) printf("video: init failed\n");
    shell_welcome();
    shell_printf("frank-blood %s on %s, RP2350%s\n", FRANK_BLOOD_VERSION, FRANK_BOARD_NAME,
                 (*(io_ro_32 *)(SYSINFO_BASE + SYSINFO_PACKAGE_SEL_OFFSET) & 1) ? "A" : "B");
    shell_printf("PSRAM: %lu KB free for the game.\n",
                 (unsigned long)((0x117F0000u - (uint32_t)__psram_bss_end) >> 10));
    usbhid_init();
    shell_printf("USB host started: keyboard, mouse, gamepad.\n");
    wait_for_sd();
    wait_for_data();
    shell_printf("SD card: /blood holds the game data (%u files checked).\n", (unsigned)N_REQUIRED);
    f_chdir("/blood");
    load_frank_cfg();
    shell_highlight("Starting Blood...\n");
    shell_end(2000);

    static const char *argv[] = { "blood", NULL };
    const int r = frank_game_main(1, argv);
    printf("game exited (%d)\n", r);
    frank_reboot();
}
