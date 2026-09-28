/*
 * C99Euclid — Euclidean pattern engine
 *
 * Responsibilities of this file:
 *   - Bresenham / Toussaint even distribution of pulses across steps
 *   - Pattern rebuild for all tracks
 *   - State defaults, clamping and validation
 *   - Rate / note name helpers
 */
#include "euclid.h"

void euclid_pattern(uint8_t *dst, int steps, int pulses, int rotate) {
    /* Fill dst[0..steps) with a Euclidean rhythm.
       Bresenham-style even distribution:
         hit(i) = ((i * pulses) % steps) < pulses
       For steps=8 pulses=3 this yields x..x..x. — the classic
       tresillo.  rotate > 0 delays the pattern (circular shift
       right).
       Inputs:
         <*uint8_t> - output buffer of at least `steps` bytes
         <int>      - cycle length (clamped 1..EU_MAX_STEPS)
         <int>      - number of hits (clamped 0..steps)
         <int>      - rotation amount (mod steps) */
    uint8_t tmp[EU_MAX_STEPS];
    int i, src;
    if (steps < 1) steps = 1;
    if (steps > EU_MAX_STEPS) steps = EU_MAX_STEPS;
    if (pulses < 0) pulses = 0;
    if (pulses > steps) pulses = steps;
    if (steps > 0) {
        rotate %= steps;
        if (rotate < 0) rotate += steps;
    } else {
        rotate = 0;
    }

    if (pulses == 0) {
        memset(dst, 0, (size_t)steps);
        return;
    }
    if (pulses >= steps) {
        for (i = 0; i < steps; i++) dst[i] = 1;
        return;
    }

    for (i = 0; i < steps; i++)
        tmp[i] = (uint8_t)(((i * pulses) % steps) < pulses ? 1 : 0);
    /* rotate > 0 delays the pattern (shift right). */
    for (i = 0; i < steps; i++) {
        src = i - rotate;
        if (src < 0) src += steps;
        dst[i] = tmp[src];
    }
}

void euclid_rebuild(const eu_state_t *st,
                    uint8_t pat[EU_TRACKS][EU_MAX_STEPS]) {
    /* Rebuild every track's cached Euclidean pattern from the
       current state.
       Inputs:
         <*eu_state_t> - source state (steps/pulses/rotate)
         <uint8_t[][]> - destination [EU_TRACKS][EU_MAX_STEPS]
                         hit matrix */
    int t;
    memset(pat, 0, (size_t)EU_TRACKS * EU_MAX_STEPS);
    for (t = 0; t < EU_TRACKS; t++) {
        const eu_track_t *tr = &st->tr[t];
        int steps = tr->steps;
        if (steps < 1) steps = 1;
        if (steps > EU_MAX_STEPS) steps = EU_MAX_STEPS;
        euclid_pattern(pat[t], steps, tr->pulses, tr->rotate);
    }
}

void eu_state_default(eu_state_t *st) {
    /* Initialise state to a playable GM drum map — drop this
       before a drum sampler.
       Inputs:
         <*eu_state_t> - state structure to fill */
    static const uint8_t notes[EU_TRACKS]  = {
        36, 38, 42, 46, 39, 43, 37, 56
    };
    static const uint8_t steps[EU_TRACKS]  = {
        16, 16, 16, 16, 16, 16, 16, 12
    };
    static const uint8_t pulses[EU_TRACKS] = {
         4,  2,  8,  2,  2,  3,  5,  5
    };
    static const uint8_t rotate[EU_TRACKS] = {
         0,  4,  0,  2,  8,  6,  1,  0
    };
    static const uint8_t vel[EU_TRACKS]    = {
       100, 96, 72, 88, 98, 84, 78, 86
    };
    static const uint8_t mute[EU_TRACKS]   = {
         0,  0,  0,  0,  0,  1,  1,  1
    };
    int t;
    memset(st, 0, sizeof(*st));
    st->magic   = EU_MAGIC;
    st->version = EU_VERSION;
    st->rate    = EU_RATE_1_16;
    st->swing   = 0;
    st->gate    = 100;
    st->learn   = EU_LEARN_OFF;
    for (t = 0; t < EU_TRACKS; t++) {
        st->tr[t].mute   = mute[t];
        st->tr[t].note   = notes[t];
        st->tr[t].ch     = 0;
        st->tr[t].steps  = steps[t];
        st->tr[t].pulses = pulses[t];
        st->tr[t].rotate = rotate[t];
        st->tr[t].vel    = vel[t];
    }
}

void eu_clamp(eu_state_t *st) {
    /* Force every field of a state structure into a legal range.
       Inputs:
         <*eu_state_t> - structure that may contain out-of-range
                         values
       Outputs:
         All numeric fields are clamped or wrapped so that
         subsequent generation and playback code can assume
         valid data. */
    int t;
    if (st->rate >= EU_RATE_COUNT) st->rate = EU_RATE_1_16;
    if (st->swing > 100) st->swing = 100;
    if (st->gate < 5) st->gate = 5;
    if (st->gate > 100) st->gate = 100;
    if (st->learn > EU_LEARN_FILL) st->learn = EU_LEARN_OFF;
    for (t = 0; t < EU_TRACKS; t++) {
        eu_track_t *tr = &st->tr[t];
        tr->mute = tr->mute ? 1 : 0;
        if (tr->steps < 1) tr->steps = 1;
        if (tr->steps > EU_MAX_STEPS) tr->steps = EU_MAX_STEPS;
        if (tr->pulses > tr->steps) tr->pulses = tr->steps;
        if (tr->rotate >= tr->steps)
            tr->rotate = (uint8_t)(tr->steps - 1);
        if (tr->ch > 15) tr->ch = 15;
        if (tr->vel < 1) tr->vel = 1;
        if (tr->vel > 127) tr->vel = 127;
        /* note is uint8, already 0..255; clamp to 127 */
        if (tr->note > 127) tr->note = 127;
    }
}

int eu_state_valid(const eu_state_t *st) {
    /* Check that a state blob has the expected magic and version.
       Inputs:
         <*eu_state_t> - state to validate
       Returns:
         <int> - non-zero if magic and version match */
    return st && st->magic == EU_MAGIC && st->version == EU_VERSION;
}

double eu_steps_per_beat(uint32_t rate) {
    /* How many Euclidean steps fit inside one quarter-note.
       Inputs:
         <uint32_t> - EU_RATE_* enumerator
       Returns:
         <double> - steps per beat (1, 2, 4 or 8) */
    switch (rate) {
    case EU_RATE_1_4:  return 1.0;
    case EU_RATE_1_8:  return 2.0;
    case EU_RATE_1_16: return 4.0;
    case EU_RATE_1_32: return 8.0;
    default:           return 4.0;
    }
}

const char *eu_rate_name(uint32_t rate) {
    /* Human-readable label for a rate enumerator.
       Inputs:
         <uint32_t> - EU_RATE_* enumerator
       Returns:
         <const char *> - static string such as "1/16" */
    switch (rate) {
    case EU_RATE_1_4:  return "1/4";
    case EU_RATE_1_8:  return "1/8";
    case EU_RATE_1_16: return "1/16";
    case EU_RATE_1_32: return "1/32";
    default:           return "1/16";
    }
}

const char *eu_note_name(uint8_t note, char *buf, size_t cap) {
    /* Format a MIDI note number as a pitch name (e.g. "C4").
       Inputs:
         <uint8_t>  - MIDI note 0..127
         <char *>   - output buffer
         <size_t>   - capacity of the output buffer
       Returns:
         <const char *> - pointer to buf (or "" on error) */
    static const char *pc[12] = {
        "C", "C#", "D", "D#", "E", "F",
        "F#", "G", "G#", "A", "A#", "B"
    };
    int n = (int)note;
    int oct;
    if (!buf || cap == 0) return "";
    if (n < 0) n = 0;
    if (n > 127) n = 127;
    oct = n / 12 - 1; /* MIDI 60 = C4 */
    snprintf(buf, cap, "%s%d", pc[n % 12], oct);
    return buf;
}
