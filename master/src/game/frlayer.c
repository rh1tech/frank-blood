/*
 * frank-blood — Build engine platform layer for the FRANK master.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Replaces jfbuild's sdlayer2.c: video is the 320x200 8-bit display
 * (display.c), input the USB host (usbhid), the timer the RP2350's
 * microsecond clock. The console keeps running from handleevents().
 */
#include "compat.h"
#include "build.h"
#include "baselayer.h"
#include "baselayer_priv.h"
#include "cache1d.h"
#include "a.h"
#include "osd.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "frank_game.h"
#include "display.h"
#include "shell.h"
#include "usb/usbhid.h"

int startwin_open(void) { return 0; }
int startwin_close(void) { return 0; }
int startwin_puts(const char *s) { (void)s; return 0; }
int startwin_idle(void *s) { (void)s; return 0; }
int startwin_settitle(const char *s) { (void)s; return 0; }
int startwin_run(struct startwin_settings *s) { (void)s; return STARTWIN_RUN; }

int wm_msgbox(const char *name, const char *fmt, ...) {
    va_list va;
    va_start(va, fmt);
    printf("[%s] ", name ? name : "");
    vprintf(fmt, va);
    printf("\n");
    va_end(va);
    return 0;
}

int wm_ynbox(const char *name, const char *fmt, ...) {
    va_list va;
    va_start(va, fmt);
    printf("[%s] ", name ? name : "");
    vprintf(fmt, va);
    printf(" -> yes\n");
    va_end(va);
    return 1;
}

int wm_filechooser(const char *initialdir, const char *initialfile, const char *type, int foropen, char **choice) {
    return -1;
}

int wm_idle(void *ptr) { return 0; }
void wm_setapptitle(const char *name) {}
void wm_setwindowtitle(const char *name) {}

int frank_game_main(int argc, const char **argv) {
    _buildargc = argc;
    _buildargv = argv;
    baselayer_init();
    return app_main(argc, argv);
}

int initsystem(void) { return 0; }
void uninitsystem(void) {}

void initputs(const char *str) {
    fputs(str, stdout);
    if (shell_active) shell_puts_quiet(str);   /* the boot log on the welcome screen */
}

void debugprintf(const char *f, ...) {
#ifdef DEBUGGINGAIDS
    va_list va;
    va_start(va, f);
    vprintf(f, va);
    va_end(va);
#endif
}

/* ---- Input ----
 * Build keys are PC set-1 scancodes, extended (E0) keys with bit 7 set. */
static const uint8_t hid_to_build[256] = {
    [0x04] = 0x1e, [0x05] = 0x30, [0x06] = 0x2e, [0x07] = 0x20, [0x08] = 0x12, /* a-e */
    [0x09] = 0x21, [0x0a] = 0x22, [0x0b] = 0x23, [0x0c] = 0x17, [0x0d] = 0x24, /* f-j */
    [0x0e] = 0x25, [0x0f] = 0x26, [0x10] = 0x32, [0x11] = 0x31, [0x12] = 0x18, /* k-o */
    [0x13] = 0x19, [0x14] = 0x10, [0x15] = 0x13, [0x16] = 0x1f, [0x17] = 0x14, /* p-t */
    [0x18] = 0x16, [0x19] = 0x2f, [0x1a] = 0x11, [0x1b] = 0x2d, [0x1c] = 0x15, /* u-y */
    [0x1d] = 0x2c,                                                             /* z */
    [0x1e] = 0x02, [0x1f] = 0x03, [0x20] = 0x04, [0x21] = 0x05, [0x22] = 0x06, /* 1-5 */
    [0x23] = 0x07, [0x24] = 0x08, [0x25] = 0x09, [0x26] = 0x0a, [0x27] = 0x0b, /* 6-0 */
    [0x28] = 0x1c, [0x29] = 0x01, [0x2a] = 0x0e, [0x2b] = 0x0f, [0x2c] = 0x39, /* ret esc bksp tab space */
    [0x2d] = 0x0c, [0x2e] = 0x0d, [0x2f] = 0x1a, [0x30] = 0x1b, [0x31] = 0x2b, /* - = [ ] \ */
    [0x33] = 0x27, [0x34] = 0x28, [0x35] = 0x29, [0x36] = 0x33, [0x37] = 0x34, /* ; ' ` , . */
    [0x38] = 0x35, [0x39] = 0x3a,                                              /* / caps */
    [0x3a] = 0x3b, [0x3b] = 0x3c, [0x3c] = 0x3d, [0x3d] = 0x3e, [0x3e] = 0x3f, /* F1-F5 */
    [0x3f] = 0x40, [0x40] = 0x41, [0x41] = 0x42, [0x42] = 0x43, [0x43] = 0x44, /* F6-F10 */
    [0x44] = 0x57, [0x45] = 0x58,                                              /* F11 F12 */
    [0x46] = 0xb7, [0x47] = 0x46, [0x48] = 0xc5,                               /* prtsc scroll pause */
    [0x49] = 0xd2, [0x4a] = 0xc7, [0x4b] = 0xc9, [0x4c] = 0xd3, [0x4d] = 0xcf, /* ins home pgup del end */
    [0x4e] = 0xd1, [0x4f] = 0xcd, [0x50] = 0xcb, [0x51] = 0xd0, [0x52] = 0xc8, /* pgdn right left down up */
    [0x53] = 0x45, [0x54] = 0xb5, [0x55] = 0x37, [0x56] = 0x4a, [0x57] = 0x4e, /* numlk kp/ kp* kp- kp+ */
    [0x58] = 0x9c, [0x59] = 0x4f, [0x5a] = 0x50, [0x5b] = 0x51, [0x5c] = 0x4b, /* kpenter kp1-4 */
    [0x5d] = 0x4c, [0x5e] = 0x4d, [0x5f] = 0x47, [0x60] = 0x48, [0x61] = 0x49, /* kp5-9 */
    [0x62] = 0x52, [0x63] = 0x53,                                              /* kp0 kp. */
    [0xe0] = 0x1d, [0xe1] = 0x2a, [0xe2] = 0x38, [0xe4] = 0x9d, [0xe5] = 0x36, /* lctrl lshift lalt rctrl rshift */
    [0xe6] = 0xb8,                                                             /* ralt */
};

static const char *const keyname_tab[256] = {
    [0x01] = "Escape", [0x02] = "1", [0x03] = "2", [0x04] = "3", [0x05] = "4", [0x06] = "5",
    [0x07] = "6", [0x08] = "7", [0x09] = "8", [0x0a] = "9", [0x0b] = "0", [0x0c] = "-",
    [0x0d] = "=", [0x0e] = "BakSpc", [0x0f] = "Tab", [0x10] = "Q", [0x11] = "W", [0x12] = "E",
    [0x13] = "R", [0x14] = "T", [0x15] = "Y", [0x16] = "U", [0x17] = "I", [0x18] = "O",
    [0x19] = "P", [0x1a] = "[", [0x1b] = "]", [0x1c] = "Enter", [0x1d] = "LCtrl", [0x1e] = "A",
    [0x1f] = "S", [0x20] = "D", [0x21] = "F", [0x22] = "G", [0x23] = "H", [0x24] = "J",
    [0x25] = "K", [0x26] = "L", [0x27] = ";", [0x28] = "'", [0x29] = "`", [0x2a] = "LShift",
    [0x2b] = "\\", [0x2c] = "Z", [0x2d] = "X", [0x2e] = "C", [0x2f] = "V", [0x30] = "B",
    [0x31] = "N", [0x32] = "M", [0x33] = ",", [0x34] = ".", [0x35] = "/", [0x36] = "RShift",
    [0x37] = "Kpad*", [0x38] = "LAlt", [0x39] = "Space", [0x3a] = "CapLck", [0x3b] = "F1",
    [0x3c] = "F2", [0x3d] = "F3", [0x3e] = "F4", [0x3f] = "F5", [0x40] = "F6", [0x41] = "F7",
    [0x42] = "F8", [0x43] = "F9", [0x44] = "F10", [0x45] = "NumLck", [0x46] = "ScrLck",
    [0x47] = "Kpad7", [0x48] = "Kpad8", [0x49] = "Kpad9", [0x4a] = "Kpad-", [0x4b] = "Kpad4",
    [0x4c] = "Kpad5", [0x4d] = "Kpad6", [0x4e] = "Kpad+", [0x4f] = "Kpad1", [0x50] = "Kpad2",
    [0x51] = "Kpad3", [0x52] = "Kpad0", [0x53] = "Kpad.", [0x57] = "F11", [0x58] = "F12",
    [0x9c] = "KpdEnt", [0x9d] = "RCtrl", [0xb5] = "Kpad/", [0xb8] = "RAlt", [0xc5] = "Pause",
    [0xc7] = "Home", [0xc8] = "Up", [0xc9] = "PgUp", [0xcb] = "Left", [0xcd] = "Right",
    [0xcf] = "End", [0xd0] = "Down", [0xd1] = "PgDn", [0xd2] = "Insert", [0xd3] = "Delete",
};

/* Unshifted / shifted ASCII for the text fifo (menus, console, cheats). */
static const char ascii_tab[2][0x3a] = {
    { 0, 27, '1','2','3','4','5','6','7','8','9','0','-','=', 8, 9,
      'q','w','e','r','t','y','u','i','o','p','[',']', 13, 0, 'a','s',
      'd','f','g','h','j','k','l',';','\'','`', 0, '\\','z','x','c','v',
      'b','n','m',',','.','/', 0, '*', 0, ' ' },
    { 0, 27, '!','@','#','$','%','^','&','*','(',')','_','+', 8, 9,
      'Q','W','E','R','T','Y','U','I','O','P','{','}', 13, 0, 'A','S',
      'D','F','G','H','J','K','L',':','"','~', 0, '|','Z','X','C','V',
      'B','N','M','<','>','?', 0, '*', 0, ' ' },
};

static char mouseacquired;

int initinput(void) {
    inputdevices = 1 | 2;
    memset(keystatus, 0, sizeof(keystatus));
    keyfifoplc = keyfifoend = 0;
    keyasciififoplc = keyasciififoend = 0;
    return 0;
}

void uninitinput(void) {}

const char *getkeyname(int num) {
    return (unsigned)num < 256 ? keyname_tab[num] : NULL;
}

const char *getjoyname(int what, int num) { return NULL; }

int initmouse(void) { mouseacquired = 1; return 0; }
void uninitmouse(void) { mouseacquired = 0; }
void grabmouse(int a) { mouseacquired = (char)a; }

void readmousexy(int *x, int *y) {
    *x = mousex;
    *y = mousey;
    mousex = mousey = 0;
}

void readmousebstatus(int *b) { *b = mouseb; }

void releaseallbuttons(void) {}

/* A key event from any source (USB, the console's `key`, scripts). */
void frank_key_event(int code, int down) {
    if (code <= 0 || code > 255) return;
    if (OSD_HandleKey(code, down) == 0) return;
    if (down) {
        keystatus[code] = 1;
        keyfifo[keyfifoend] = code;
        keyfifo[(keyfifoend + 1) & (KEYFIFOSIZ - 1)] = 1;
        keyfifoend = (keyfifoend + 2) & (KEYFIFOSIZ - 1);
        if (code < 0x3a) {
            const int shift = keystatus[0x2a] || keystatus[0x36];
            const char ch = ascii_tab[shift][code];
            if (ch && OSD_HandleChar(ch) &&
                ((keyasciififoend + 1) & (KEYFIFOSIZ - 1)) != keyasciififoplc) {
                keyasciififo[keyasciififoend] = (unsigned char)ch;
                keyasciififoend = (keyasciififoend + 1) & (KEYFIFOSIZ - 1);
            }
        }
    } else {
        keystatus[code] = 0;
    }
}

static void poll_usb(void) {
    usbhid_task();
    uint8_t hid;
    int down;
    while (usbhid_get_key_action(&hid, &down))
        frank_key_event(hid_to_build[hid], down);
    if (mouseacquired) {
        usbhid_mouse_state_t m;
        usbhid_get_mouse_state(&m);
        if (m.has_motion) {
            mousex += m.dx;
            mousey -= m.dy;   /* usbhid gives up = positive; Build wants USB's down = positive */
        }
        mouseb = m.buttons & 7;   /* a click without motion too */
    }
}

/* ---- Timer ---- */
static uint32_t timerticspersec;
static uint64_t timerlastsample;
static void (*usertimercallback)(void);
int frank_fixed_ticks;
static int frank_idle_samples;

int inittimer(int tickspersecond, void (*callback)(void)) {
    if (timerticspersec) return 0;
    timerticspersec = (uint32_t)tickspersecond;
    timerlastsample = time_us_64() * timerticspersec / 1000000u;
    usertimercallback = callback;
    return 0;
}

void uninittimer(void) { timerticspersec = 0; }

void frank_timer_advance(int n) {
    frank_idle_samples = 0;
    totalclock += n;
    if (usertimercallback) for (; n > 0; n--) usertimercallback();
}

void sampletimer(void) {
    if (!timerticspersec) return;
    if (frank_fixed_ticks > 0) {
        /* Same rule as the host build: waits without frames still advance. */
        if (++frank_idle_samples >= 1000) {
            frank_idle_samples = 0;
            totalclock++;
            if (usertimercallback) usertimercallback();
        }
        return;
    }
    const uint64_t now = time_us_64() * timerticspersec / 1000000u;
    int n = (int)(now - timerlastsample);
    if (n > 0) {
        totalclock += n;
        timerlastsample += (uint64_t)n;
    }
    if (usertimercallback) for (; n > 0; n--) usertimercallback();
}

unsigned int getticks(void) { return (unsigned int)(time_us_64() / 1000u); }
unsigned int getusecticks(void) { return (unsigned int)time_us_64(); }
int gettimerfreq(void) { return (int)timerticspersec; }

/* ---- Video ---- */
static char modeschecked;

void getvalidmodes(void) {
    if (modeschecked) return;
    validmodecnt = 0;
    validmode[0].xdim = DISPLAY_W;
    validmode[0].ydim = DISPLAY_H;
    validmode[0].bpp = 8;
    validmode[0].fs = 1;
    validmode[1] = validmode[0];
    validmode[1].fs = 0;
    validmodecnt = 2;
    modeschecked = 1;
}

int setvideomode(int x, int y, int c, int fs) {
    if (c != 8 || x != DISPLAY_W || y != DISPLAY_H) {
        x = DISPLAY_W;
        y = DISPLAY_H;
        c = 8;
    }
    if (xres == x && yres == y && bpp == c && !videomodereset) {
        OSD_ResizeDisplay(xres, yres);
        return 0;
    }
    if (baselayer_videomodewillchange) baselayer_videomodewillchange();

    frameplace = (intptr_t)display_backbuffer();
    bytesperline = DISPLAY_W;
    imageSize = DISPLAY_W * DISPLAY_H;
    numpages = 1;
    setvlinebpl(bytesperline);
    for (int i = 0, j = 0; i <= y; i++, j += bytesperline) ylookup[i] = j;

    xres = x;
    yres = y;
    bpp = c;
    fullscreen = fs;
    modechange = 1;
    videomodereset = 0;
    if (baselayer_videomodedidchange) baselayer_videomodedidchange();
    OSD_ResizeDisplay(xres, yres);
    setpalettefade(palfadergb.r, palfadergb.g, palfadergb.b, palfadedelta);
    return 0;
}

void resetvideomode(void) {
    videomodereset = 1;
    modeschecked = 0;
}

/* The game's palette, held while the boot shell is on screen. */
static uint8_t held_rgb[256 * 3];
static bool held;

void showframe(void) {
    if (shell_active) shell_release(held ? held_rgb : NULL);   /* the first game frame */
    frank_frame_shown((const uint8_t *)frameplace);
    /* The game draws into one persistent buffer (numpages 1): swap, then
     * carry the shown picture into the new back buffer. */
    uint8_t *back = display_flip();
    memcpy(back, (const void *)frameplace, DISPLAY_W * DISPLAY_H);
    frameplace = (intptr_t)back;
}

int setpalette(int start, int num, unsigned char *dapal) {
    uint8_t rgb[256 * 3];
    uint8_t *out = shell_active ? held_rgb : rgb;
    for (int i = 0; i < 256; i++) {
        out[i * 3 + 0] = curpalettefaded[i].r;
        out[i * 3 + 1] = curpalettefaded[i].g;
        out[i * 3 + 2] = curpalettefaded[i].b;
    }
    if (shell_active) held = true;   /* applied when the shell lets go */
    else display_set_palette(rgb, 0, 256);
    return 0;
}

int setgamma(float gamma) { return 0; }

/* ---- Events ---- */
int handleevents(void) {
    poll_usb();
    frank_service();
    sampletimer();
    return quitevent ? -1 : 0;
}
