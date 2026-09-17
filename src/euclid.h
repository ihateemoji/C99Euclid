#ifndef EUCLID_DOT_H
#define EUCLID_DOT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <sys/timerfd.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <clap/clap.h>
#include <clap/host.h>
#include <clap/ext/gui.h>
#include <clap/ext/log.h>
#include <clap/ext/params.h>
#include <clap/ext/posix-fd-support.h>
#include <clap/ext/state.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846264338327950288
#endif

/* Euclid RPE — pure C99 Euclidean rhythm engine.
   Distributes `pulses` hits as evenly as possible across `steps`
   (Bresenham / Toussaint), then rotates. Used by the CLAP plugin
   and mirrored in the web lab. */

#define EU_MAGIC          0x45555031u /* "EUP1" */
#define EU_VERSION        1u
#define EU_TRACKS         8
#define EU_MAX_STEPS      32
#define EU_MAX_VOICES     16

enum {
    EU_RATE_1_4  = 0,
    EU_RATE_1_8  = 1,
    EU_RATE_1_16 = 2,
    EU_RATE_1_32 = 3,
    EU_RATE_COUNT = 4
};

enum {
    EU_LEARN_OFF  = 0,  /* ignore incoming notes */
    EU_LEARN_T1   = 1,  /* write incoming note onto track 1..8 */
    EU_LEARN_T8   = 8,
    EU_LEARN_FILL = 9   /* round-robin fill tracks from incoming notes */
};

typedef struct {
    uint8_t mute;    /* 0 = play, 1 = muted */
    uint8_t note;    /* MIDI note 0..127 */
    uint8_t ch;      /* MIDI channel 0..15 */
    uint8_t steps;   /* 1..32 */
    uint8_t pulses;  /* 0..steps  (triggers) */
    uint8_t rotate;  /* 0..steps-1 */
    uint8_t vel;     /* 1..127 */
} eu_track_t;

typedef struct {
    uint32_t  magic;
    uint32_t  version;
    uint32_t  rate;     /* EU_RATE_* */
    uint32_t  swing;    /* 0..75 percent */
    uint32_t  gate;     /* 5..95 percent of one step */
    uint32_t  learn;    /* EU_LEARN_* */
    eu_track_t tr[EU_TRACKS];
} eu_state_t;

/* Fill dst[0..steps) with 0/1 hits. rotate delays the pattern. */
void euclid_pattern(uint8_t *dst, int steps, int pulses, int rotate);

/* Rebuild every track's cached pattern. */
void euclid_rebuild(const eu_state_t *st, uint8_t pat[EU_TRACKS][EU_MAX_STEPS]);

void eu_state_default(eu_state_t *st);
void eu_clamp(eu_state_t *st);
int  eu_state_valid(const eu_state_t *st);

/* How many steps of the Euclidean clock fit in one quarter-note. */
double eu_steps_per_beat(uint32_t rate);

const char *eu_rate_name(uint32_t rate);
const char *eu_note_name(uint8_t note, char *buf, size_t cap);

typedef struct {
    int     live;
    int32_t pitch;
    int32_t note_id;
    int64_t off_abs;
    int     ch;
} eu_voice_t;

/* Full plugin state (shared between plugin.c and gui_x11.c) */
typedef struct {
    clap_plugin_t plugin;
    const clap_host_t *host;
    const clap_host_log_t *host_log;
    const clap_host_params_t *host_params;
    const clap_host_state_t *host_state;
    const clap_host_gui_t *host_gui;

    eu_state_t st;
    uint8_t    pat[EU_TRACKS][EU_MAX_STEPS];
    int        dirty;

    double  sr;
    double  tempo;
    int64_t abs_sample;
    int     active;
    int     processing;
    int     next_note_id;
    int     learn_next;
    int64_t last_gstep;

    eu_voice_t voices[EU_TRACKS];

    /* X11 GUI */
    Display *dpy;
    Window   win;
    GC       gc;
    int      gui_w, gui_h;
    int      gui_created;
    int      gui_visible;
    int      xfd;
    int      timer_fd;
    const clap_host_posix_fd_support_t *host_fd;
} eu_plug_t;

/* GUI / FD extensions implemented in gui_x11.c */
extern const clap_plugin_gui_t eu_gui_ext;
extern const clap_plugin_posix_fd_support_t eu_posix_fd_ext;

#endif /* EUCLID_DOT_H */
