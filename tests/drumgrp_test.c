/* SPDX-License-Identifier: GPL-3.0-only */
/* HYPNO-1: the drum track's lane groups (drums.c LANE_GROUP, seq.c drum_compose). Each group, KICK, RUMBLE, HATS
 * and PERC, loops over its own length of the drum track's steps (the drum track's P_DGLEN + group, 0 = the
 * track's LEN):
 *   default    every group at 0: the drum track plays as in SLOOP (a kick on 1 and 3 of a 16-step loop)
 *   polymeter  hats on a 3-step loop against a 4-on-the-floor kick: each on its own grid, no drift in 4 bars
 *   longer     a group longer than the track's LEN plays its steps past LEN
 *   ratchets   a ratchet on a hat of a 3-step loop plays its extra hits on that loop
 *   record     a hit recorded live lands in its group's loop
 *   project    the loop lengths survive a save and a load; a project with 0 there plays as the track
 * Exit status: the number of failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
static uint32_t trk_def_engine(uint32_t i) { return i < NPART ? i : 0; }
#include "../firmware/src/project.c"

#define N_KICK 36u               /* LANE_NOTE[0] */
#define N_HAT 42u                /* LANE_NOTE[4], group HATS */
#define N_CLAP 39u               /* LANE_NOTE[3], group PERC */

typedef struct { uint64_t blk; uint8_t note; } hit_t;
static hit_t hits[100000];
static uint32_t nhits;
static uint64_t blk;
static void run_block(void)
{
    int32_t out[CTL * 2];
    uint32_t a = drums.age, k;
    mix_block(out, CTL);
    if (drums.age != a)
        for (k = 0; k < NDRUM; k++)
            if (drums.v[k].age > a && nhits < 100000u) {
                hits[nhits].blk = blk;
                hits[nhits].note = drums.v[k].note;
                nhits++;
            }
    blk++;
}
static void reset(void)
{
    uint32_t i;
    if (song.playing)
        seq_stop();                                  /* (each case plays from the top) */
    clk_beat = clk_pos = 0;
    host_tracks_init();
    for (i = 0; i < NTRK; i++)
        steps_clear(&trk[i]);
    memset(&drums, 0, sizeof drums);
    drums.set = -2;
    nhits = 0;
    blk = 0;
    song.g[G_BPM] = 120;
    song.rec = 0;
    TDRUM->p[P_SDIV] = 2;                            /* 1/16 */
    TDRUM->p[P_SLEN] = 16;
    TDRUM->p[P_SSWING] = 0;
    song.g[G_SWING] = 0;
}
/* play `steps` sixteenths from PLAY; the grid step of each hit is its block's time / a step's length */
static void play_steps(uint32_t steps)
{
    transport_req = 1;
    while (clk_beat * 4u + clk_pos / (BEAT_U / 4u) < steps)
        run_block();
}
static uint32_t step_of(uint64_t b)
{
    double spb = FS * 60.0 / 120.0 / 4.0 / CTL;      /* blocks a sixteenth */
    return (uint32_t)((double)b / spb + 0.5);
}
static uint32_t count(uint32_t note)
{
    uint32_t i, n = 0;
    for (i = 0; i < nhits; i++)
        n += hits[i].note == note;
    return n;
}
/* every hit of `note` falls on a step that is a multiple of `every` (and none is missing in `steps`) */
static int on_grid(uint32_t note, uint32_t every, uint32_t steps)
{
    uint32_t i, n = 0;
    for (i = 0; i < nhits; i++)
        if (hits[i].note == note) {
            if (step_of(hits[i].blk) % every)
                return 0;
            n++;
        }
    return n == (steps + every - 1u) / every;
}

static int fails;
static void check(int ok, const char *what)
{
    uint32_t i;
    printf("drumgrp: %-70s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
    if (!ok) {                                       /* what played: note@step */
        printf("drumgrp:   %u hits:", nhits);
        for (i = 0; i < nhits && i < 40u; i++)
            printf(" %u@%u", hits[i].note, step_of(hits[i].blk));
        printf("\n");
    }
}

static void t_default(void)
{
    reset();
    dstep_set(&TDRUM->dstep[0], 0, LV_NORM, 0);
    dstep_set(&TDRUM->dstep[8], 0, LV_NORM, 0);
    play_steps(64);
    check(count(N_KICK) == 8u && on_grid(N_KICK, 8, 64), "every group at 0: a 16-step loop as in SLOOP (8 kicks in 4 bars)");
}

static void t_polymeter(void)
{
    uint32_t i;
    reset();
    for (i = 0; i < 16u; i += 4u)
        dstep_set(&TDRUM->dstep[i], 0, LV_NORM, 0);  /* the kick on every beat */
    dstep_set(&TDRUM->dstep[0], 4, LV_NORM, 0);      /* a hat on step 1 of a 3-step loop */
    TDRUM->p[P_DGLEN + DG_HATS] = 3;
    play_steps(64);
    check(on_grid(N_KICK, 4, 64), "KICK at the track's 16: on every beat for 4 bars");
    check(on_grid(N_HAT, 3, 64), "HATS on a 3-step loop: every third sixteenth, 22 in 4 bars, no drift");
}

static void t_longer(void)
{
    reset();
    TDRUM->p[P_SLEN] = 16;
    TDRUM->p[P_DGLEN + DG_PERC] = 24;
    dstep_set(&TDRUM->dstep[20], 3, LV_NORM, 0);     /* a clap on step 21: past the track's LEN */
    play_steps(48);
    check(count(N_CLAP) == 2u && step_of(hits[0].blk) == 20u, "PERC at 24 over a 16-step track: its step 21 plays (twice in 3 bars)");
}

static void t_ratchet(void)
{
    reset();
    TDRUM->p[P_DGLEN + DG_HATS] = 3;
    dstep_set(&TDRUM->dstep[0], 4, LV_NORM, 1);      /* x2 */
    play_steps(12);
    check(count(N_HAT) == 8u, "a x2 hat on a 3-step loop: 2 hits on each of its 4 passes in 12 steps");
}

static void t_record(void)
{
    uint32_t i, at = 0xFFu;
    reset();
    TDRUM->p[P_DGLEN + DG_HATS] = 3;
    song.rec = 1u << TRK_DRUM;
    transport_req = 1;
    while (clk_beat * 4u + clk_pos / (BEAT_U / 4u) < 7u)
        run_block();
    rec_hit(TDRUM, 4, LV_NORM, 0);                  /* a hat recorded on grid step 7 */
    for (i = 0; i < NSTEP; i++)
        if (dstep_has(&TDRUM->dstep[i], 4))
            at = i;
    check(at < 3u, "a hat recorded live on step 8 lands in the HATS loop of 3 (its step 2 or 3)");
}

static void t_project(void)
{
    static project_t p;
    reset();
    TDRUM->p[P_DGLEN + DG_KICK] = 0;
    TDRUM->p[P_DGLEN + DG_RUMBLE] = 32;
    TDRUM->p[P_DGLEN + DG_HATS] = 3;
    TDRUM->p[P_DGLEN + DG_PERC] = 64;
    proj_capture(&p);
    TDRUM->p[P_DGLEN + DG_HATS] = 7;
    proj_apply(&p, 1);
    check(TDRUM->p[P_DGLEN + DG_KICK] == 0 && TDRUM->p[P_DGLEN + DG_RUMBLE] == 32 && TDRUM->p[P_DGLEN + DG_HATS] == 3 &&
              TDRUM->p[P_DGLEN + DG_PERC] == 64,
          "the loop lengths 0 / 32 / 3 / 64 come back from a saved project");
    pt_set(&p.t[TRK_DRUM], P_DGLEN + DG_HATS, 0);    /* a SLOOP project: 0 there */
    p.sum = proj_sum(&p);
    proj_apply(&p, 1);
    check(lane_len(TDRUM, 4) == trk_len(TDRUM), "a project with 0 there: the hats follow the track's LEN");
}

int main(void)
{
    t_default();
    t_polymeter();
    t_longer();
    t_ratchet();
    t_record();
    t_project();
    printf("drumgrp: %s\n", fails ? "FAILED" : "all checks ok");
    return fails;
}
