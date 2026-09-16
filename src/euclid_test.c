#include "euclid.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static void dump(const uint8_t *p, int n, char *out)
{
    int i;
    for (i = 0; i < n; i++) out[i] = p[i] ? 'x' : '.';
    out[n] = 0;
}

static int expect(const char *label, int steps, int pulses, int rot, const char *want)
{
    uint8_t p[EU_MAX_STEPS];
    char got[EU_MAX_STEPS + 1];
    euclid_pattern(p, steps, pulses, rot);
    dump(p, steps, got);
    if (strcmp(got, want) != 0) {
        fprintf(stderr, "FAIL %s  steps=%d pulses=%d rot=%d\n  got  %s\n  want %s\n",
                label, steps, pulses, rot, got, want);
        return 1;
    }
    printf("ok   %s  %s\n", label, got);
    return 0;
}

int main(void)
{
    int fail = 0;
    eu_state_t st;
    uint8_t pat[EU_TRACKS][EU_MAX_STEPS];
    char buf[16];

    fail += expect("8/3",     8, 3, 0, "x..x..x.");
    fail += expect("8/3 r1",  8, 3, 1, ".x..x..x");
    fail += expect("16/4",   16, 4, 0, "x...x...x...x...");
    fail += expect("16/4 r1",16, 4, 1, ".x...x...x...x..");
    fail += expect("5/3",     5, 3, 0, "x.x.x");
    fail += expect("7/3",     7, 3, 0, "x..x.x.");
    fail += expect("12/5",   12, 5, 0, "x..x.x..x.x.");
    fail += expect("1/1",     1, 1, 0, "x");
    fail += expect("8/0",     8, 0, 0, "........");
    fail += expect("8/8",     8, 8, 0, "xxxxxxxx");

    eu_state_default(&st);
    eu_clamp(&st);
    if (!eu_state_valid(&st)) { fprintf(stderr, "FAIL default magic\n"); fail++; }
    euclid_rebuild(&st, pat);
    if (pat[0][0] != 1) { fprintf(stderr, "FAIL kick step0\n"); fail++; }
    if (pat[1][4] != 1) { fprintf(stderr, "FAIL snare rot4\n"); fail++; }

    eu_note_name(36, buf, sizeof buf);
    if (strcmp(buf, "C2") != 0) { fprintf(stderr, "FAIL note name %s\n", buf); fail++; }
    eu_note_name(60, buf, sizeof buf);
    if (strcmp(buf, "C4") != 0) { fprintf(stderr, "FAIL C4 %s\n", buf); fail++; }

    if (eu_steps_per_beat(EU_RATE_1_16) != 4.0) { fprintf(stderr, "FAIL rate\n"); fail++; }

    if (fail) {
        fprintf(stderr, "%d failure(s)\n", fail);
        return 1;
    }
    printf("all tests passed\n");
    return 0;
}
