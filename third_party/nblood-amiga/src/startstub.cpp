// Startup-window hooks for builds without a launcher window
// (FRANK board and its host reference build).
#include "compat.h"
#include "baselayer.h"

int startwin_open(void) { return 0; }
int startwin_close(void) { return 0; }
int startwin_puts(const char *) { return 0; }
int startwin_settitle(const char *) { return 0; }
int startwin_idle(void *) { return 0; }
int startwin_run(struct startwin_settings *) { return 0; }
