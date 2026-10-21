/* SPDX-License-Identifier: GPL-3.0-only */
/* HYPNO-1 stage 4, part 2: the sequencer's step CHANCE and CONDITION, the ACCUMULATOR, the LOOP WINDOW and MUTATE
 * (seq.c, dice.c), played through the sequencer (mix_block) and read off what each step fired:
 *   chance     50 % plays about half the passes (35..65 % of 400), 100 % every one
 *   condition  1:4 on passes 1, 5, 9 ..; 2:2 on the even ones; 1ST only on the first, !1ST never on it; PRE as the
 *              condition before it came out, !PRE the other way
 *   drums      a hat with 1:2 under a kick on the same step: the kick every pass, the hat every other
 *   acc        +1 degree a pass over 4 passes in C minor: C D Eb F, then C again
 *   loop       LOOP WINDOW 4 engaged on step 7 of 16: 5 6 7 8 5 6 ..; let go: on where the grid is
 *   mutate     a line changed, its notes still in the scale; the drums' four on the floor kept
 * Exit status: the number of failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int fails;
static void check(int ok, const char *what)
{
    printf("seq4: %-84s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}
static void setup(uint32_t len)
{
    if (song.playing)
        seq_stop();
    host_tracks_init();
    loop_win = 0;
    host_preset(&trk[0], ENGI_ACID, 0);
    trk[0].p[P_SLEN] = (int16_t)len;
    trk[0].p[P_SCALE] = 2;                             /* MIN, C */
    trk[0].p[P_ROOT] = 0;
    song.g[G_BPM] = 240;
}
static void note_at(uint32_t i, uint32_t n)
{
    step_t *s = &trk[0].step[i];
    memset(s, 0, sizeof *s);
    s->n = 1, s->note[0] = (uint8_t)n, s->time = ST_NOTE, s->vel = 100;
    trk[0].seq_active = 1;
}
/* play until track t fires `steps` more steps; per step: the note it sounds (0 = none) / the drum step composed */
static uint8_t got[512], idx_of[512];
static dstep_t dgot[512];
static void play(track_t *t, uint32_t steps)
{
    int32_t o[2 * CTL];
    uint32_t n = 0, last = t->seq_abs, guard = 0;
    if (!song.playing)
        transport_req = 1;
    while (n < steps && guard++ < steps * 400u) {
        mix_block(o, CTL);
        if (t->seq_abs != last && t->seq_abs != SEQ_NONE) {
            last = t->seq_abs;
            got[n] = t->seq_skip || !t->seq_n ? 0 : t->seq_notes[0];
            idx_of[n] = (uint8_t)t->seq_idx;
            dgot[n] = drum_now;
            n++;
        }
    }
}

static void t_chance(void)
{
    uint32_t i, on = 0, all = 1;
    setup(1);
    note_at(0, 48);
    cond_set(&trk[0], 0, 0, 50, SC_NONE);
    play(&trk[0], 400);
    for (i = 0; i < 400u; i++)
        on += got[i] != 0;
    check(on > 140u && on < 260u, "CHANCE 50 %: about half of 400 passes play");
    printf("seq4:   %u of 400\n", on);
    setup(1);
    note_at(0, 48);
    cond_set(&trk[0], 0, 0, 50, SC_NONE);
    cond_set(&trk[0], 0, 0, 100, SC_NONE);
    play(&trk[0], 50);
    for (i = 0; i < 50u; i++)
        all &= got[i] == 48u;
    check(all && lock_find(&trk[0], 0, PL_COND, 0) < 0, "CHANCE back to 100 %: every pass, and no lock left");
}

static void t_cond(void)
{
    uint32_t i, a = 1, b = 1, f = 1, nf = 1, pre = 1;
    setup(1);
    note_at(0, 48);
    cond_set(&trk[0], 0, 0, 100, SC_1_4);
    play(&trk[0], 24);
    for (i = 0; i < 24u; i++)
        a &= (got[i] != 0) == (i % 4u == 0u);
    check(a, "COND 1:4: passes 1, 5, 9 .. only");
    setup(1);
    note_at(0, 48);
    cond_set(&trk[0], 0, 0, 100, SC_2_2);
    play(&trk[0], 12);
    for (i = 0; i < 12u; i++)
        b &= (got[i] != 0) == (i % 2u == 1u);
    check(b, "COND 2:2: every second pass");
    setup(1);
    note_at(0, 48);
    cond_set(&trk[0], 0, 0, 100, SC_FIRST);
    play(&trk[0], 8);
    for (i = 0; i < 8u; i++)
        f &= (got[i] != 0) == (i == 0u);
    setup(1);
    note_at(0, 48);
    cond_set(&trk[0], 0, 0, 100, SC_NFIRST);
    play(&trk[0], 8);
    for (i = 0; i < 8u; i++)
        nf &= (got[i] != 0) == (i != 0u);
    check(f && nf, "COND 1ST: the first pass only; !1ST: all but it");
    setup(2);                                          /* step 1: 1:2; step 2: PRE (as step 1 came out) */
    note_at(0, 48);
    note_at(1, 51);
    cond_set(&trk[0], 0, 0, 100, SC_1_2);
    cond_set(&trk[0], 1, 0, 100, SC_PRE);
    play(&trk[0], 16);
    for (i = 0; i + 1u < 16u; i += 2u)
        pre &= (got[i] != 0) == (got[i + 1u] != 0) && (got[i] != 0) == (i % 4u == 0u);
    check(pre, "COND PRE: the step after a 1:2 plays when it did");
}

static void t_drums(void)
{
    uint32_t i, kick = 1, hat = 1;
    setup(1);
    TDRUM->p[P_SLEN] = 1;
    memset(TDRUM->dstep, 0, sizeof TDRUM->dstep);
    dstep_set(&TDRUM->dstep[0], 0, LV_NORM, 0);       /* kick */
    dstep_set(&TDRUM->dstep[0], 4, LV_NORM, 0);       /* hat, 1:2 */
    TDRUM->seq_active = 1;
    cond_set(TDRUM, 0, 4, 100, SC_1_2);
    play(TDRUM, 12);
    for (i = 0; i < 12u; i++) {
        kick &= dstep_has(&dgot[i], 0);
        hat &= dstep_has(&dgot[i], 4) == (i % 2u == 0u);
    }
    check(kick && hat, "DRUMS: a hat with 1:2 under a kick on the same step: the kick always, the hat every other");
}

static void t_acc(void)
{
    static const uint8_t WANT[8] = {48, 50, 51, 53, 48, 50, 51, 53};   /* C D Eb F (C minor), again */
    uint32_t i, ok = 1;
    int32_t d;
    uint32_t p;
    setup(1);
    note_at(0, 48);
    acc_set(&trk[0], 1, 4);
    acc_get(&trk[0], &d, &p);
    play(&trk[0], 8);
    for (i = 0; i < 8u; i++)
        ok &= got[i] == WANT[i];
    check(d == 1 && p == 4u && ok, "ACC +1/4 in C minor: C D Eb F, then C again");
    printf("seq4:   %u %u %u %u %u\n", got[0], got[1], got[2], got[3], got[4]);
    acc_set(&trk[0], 0, 4);
    check(lock_find(&trk[0], 0, PL_ACC, 0) < 0, "ACC 0: off, no lock left");
}

static void t_loop(void)
{
    uint32_t i, ok = 1, back;
    setup(16);
    for (i = 0; i < 16u; i++)
        note_at(i, 36u + i);
    play(&trk[0], 6);                                  /* steps 1..6 played */
    loop_win = 4;
    play(&trk[0], 10);
    for (i = 0; i < 10u; i++)
        ok &= idx_of[i] == 4u + (6u + i) % 4u;       /* steps 7 8, then 5 6 7 8 5 6 7 8 (index 4..7) */
    check(ok, "LOOP 4 from step 7: 7 8 5 6 7 8 .. (the beat it was in)");
    loop_win = 0;
    play(&trk[0], 3);
    back = idx_of[0];
    check(back == 0u && idx_of[1] == 1u, "  let go: on where the grid is (step 17 = 1 of the next bar)");
    printf("seq4:   after the loop: %u %u %u\n", idx_of[0], idx_of[1], idx_of[2]);
}

static void t_mutate(void)
{
    static step_t before[16];
    uint32_t i, inscale = 1, kick = 1, changed;
    setup(16);
    song.g[G_DWILD] = 100;
    dice_roll(DT_T1);
    memcpy(before, trk[0].step, sizeof before);
    dice_mutate(DT_T1);
    changed = memcmp(before, trk[0].step, sizeof before) != 0;
    for (i = 0; i < 16u; i++)
        if (trk[0].step[i].time == ST_NOTE)
            inscale &= (scale_mask(&trk[0]) >> (trk[0].step[i].note[0] % 12u)) & 1u;
    dice_roll(DT_BEAT);
    for (i = 0; i < 20u; i++)
        dice_mutate(DT_BEAT);
    for (i = 0; i < 16u; i += 4u)
        kick &= dstep_has(&TDRUM->dstep[i], 0);
    check(changed && inscale, "MUTATE a line: changed, every note still in the scale");
    check(kick, "MUTATE the beat 20 times: the kick still four to the floor");
}

int main(void)
{
    t_chance();
    t_cond();
    t_drums();
    t_acc();
    t_loop();
    t_mutate();
    printf("seq4: %s\n", fails ? "FAILED" : "all checks ok");
    return fails;
}
