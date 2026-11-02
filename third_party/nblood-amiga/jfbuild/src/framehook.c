// Host reference build: per-frame CRC and PPM dumps of the 8-bit frame.
//   FRANK_CRC=1            print "frame <n> crc <crc32>" for every shown frame
//   FRANK_DUMP=<dir>       write <dir>/fNNNNN.ppm ...
//   FRANK_DUMP_EVERY=<n>   ... every n-th frame (default 35)
//   FRANK_QUIT_AT=<n>      exit after frame n
//   FRANK_HEADLESS=1       no window (SDL dummy video driver)
//   FRANK_TICKS=<n>        deterministic: the 120 Hz timer advances n ticks per
//                          shown frame instead of by real time
//   FRANK_KEYS=<list>      scripted keys, "frame:scancode_hex:1|0,...", e.g.
//                          "100:48:1,160:48:0" holds Up from frame 100 to 160
// A crash prints a backtrace to stderr and exits with 128+signal, so the
// macOS crash reporter never shows a dialog.

#include "build.h"
#include "baselayer.h"
#include "crc32.h"

#include <execinfo.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

extern int frank_fixed_ticks;
void frank_timer_advance(int n);
void frank_inject_key(int code, int state);
void frank_demo_close(void);

#define MAXSCRIPTKEYS 512
static struct { unsigned frame; int code, state; } scriptkeys[MAXSCRIPTKEYS];
static int nscriptkeys, nextscriptkey;

static void parse_keys(const char *s)
{
	while (s && *s && nscriptkeys < MAXSCRIPTKEYS) {
		unsigned f; int code, state, used = 0;
		if (sscanf(s, "%u:%x:%d%n", &f, &code, &state, &used) != 3) break;
		scriptkeys[nscriptkeys].frame = f;
		scriptkeys[nscriptkeys].code = code;
		scriptkeys[nscriptkeys].state = state;
		nscriptkeys++;
		s += used;
		if (*s == ',') s++;
	}
}

int frank_demo_menu = 1;   // demos under the main menu (FRANK_DEMOMENU=0: plain)
static unsigned frames_total;
unsigned frank_frames_shown(void) { return frames_total; }

static void crash_handler(int sig)
{
	void *pcs[64];
	int n = backtrace(pcs, 64);
	fprintf(stderr, "\nfatal signal %d\n", sig);
	backtrace_symbols_fd(pcs, n, 2);
	_exit(128 + sig);
}

void frank_host_init(void)
{
	signal(SIGSEGV, crash_handler);
	signal(SIGBUS, crash_handler);
	signal(SIGILL, crash_handler);
	signal(SIGFPE, crash_handler);
	signal(SIGABRT, crash_handler);
	if (getenv("FRANK_HEADLESS"))
		setenv("SDL_VIDEODRIVER", "dummy", 1);
	if (getenv("FRANK_TICKS"))
		frank_fixed_ticks = atoi(getenv("FRANK_TICKS"));
	parse_keys(getenv("FRANK_KEYS"));
	if (getenv("FRANK_DEMOMENU"))
		frank_demo_menu = atoi(getenv("FRANK_DEMOMENU"));
}

void frank_frame_hook(const unsigned char *frame, int pitch, int w, int h)
{
	static int init, docrc, every = 35, quitat = -1;
	static const char *dumpdir;
	static unsigned frameno;
	int x, y;

	if (!init) {
		const char *s;
		init = 1;
		docrc = getenv("FRANK_CRC") != NULL;
		dumpdir = getenv("FRANK_DUMP");
		if ((s = getenv("FRANK_DUMP_EVERY")) != NULL) every = atoi(s);
		if (every < 1) every = 1;
		if ((s = getenv("FRANK_QUIT_AT")) != NULL) quitat = atoi(s);
	}

	if (docrc) {
		unsigned crc = 0;
		crc32init(&crc);
		for (y = 0; y < h; y++) crc32block(&crc, (unsigned char *)frame + y * pitch, w);
		crc = crc32finish(&crc);
		fprintf(stderr, "frame %u crc %08x\n", frameno, crc);
	}

	if (dumpdir && frameno % every == 0) {
		char path[1024];
		FILE *f;
		snprintf(path, sizeof(path), "%s/f%05u.ppm", dumpdir, frameno);
		if ((f = fopen(path, "wb")) != NULL) {
			fprintf(f, "P6\n%d %d\n255\n", w, h);
			for (y = 0; y < h; y++)
				for (x = 0; x < w; x++) {
					const palette_t *p = &curpalettefaded[frame[y * pitch + x]];
					fputc(p->r, f); fputc(p->g, f); fputc(p->b, f);
				}
			fclose(f);
		}
	}

	if (quitat >= 0 && (int)frameno >= quitat) {
		frank_demo_close();
		exit(0);
	}
	frameno++;
	frames_total = frameno;

	while (nextscriptkey < nscriptkeys && scriptkeys[nextscriptkey].frame <= frameno) {
		frank_inject_key(scriptkeys[nextscriptkey].code, scriptkeys[nextscriptkey].state);
		nextscriptkey++;
	}
	if (frank_fixed_ticks > 0)
		frank_timer_advance(frank_fixed_ticks);
}

// Demo benchmark marks (demo.cpp): frames per demo; the host counts frames
// only (its timing is not the board's).
static unsigned demo_start_frame;
void frank_demo_mark(int start, const char *level)
{
	extern unsigned frank_frames_shown(void);
	if (start) demo_start_frame = frank_frames_shown();
	else fprintf(stderr, "demo %s: %u frames\n", level, frank_frames_shown() - demo_start_frame);
}
