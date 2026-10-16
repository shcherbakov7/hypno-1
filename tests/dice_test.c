/* SPDX-License-Identifier: GPL-3.0-only */
/* HYPNO-1 stage 4: DICE (dice.c), its rules over many rolls of every style:
 *   beat     the kick four to the floor; KICK 2 (the rumble) never on a kick; the open hat on the off-beats; the
 *            perc group 16 steps or an odd 12 / 13 / 15; a clap on 2 and 4 where the style has one
 *   seed     the same roll gives the same pattern; rolls differ (variety)
 *   acid     the line in the track's scale, step 1 the tonic, the tonic the most played note; accents and slides
 *            with WILD, few without
 *   tracks   track 2 an off-beat bass, track 4 a drone (one note held over the 16 steps)
 *   history  OCT- the roll before, OCT+ the one after; past the newest a new roll; 16 kept
 *   undo     a roll is one undo
 * Writes build/host/dice-*.wav (a rolled beat with an acid line) to listen. Exit status: the failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int fails;
static void check(int ok, const char *what)
{
    printf("dice: %-84s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}
static int on(uint32_t i, uint32_t lane) { return dstep_has(&TDRUM->dstep[i], lane); }
static void setup(void)
{
    host_tracks_init();
    host_preset(&trk[0], ENGI_ACID, 0);
    dice_n = dice_at = 0;
}
static void t_beat(void)
{
    uint32_t st, n, i, kick = 1, rmbl = 1, open = 1, plen = 1, clap = 1, odd = 0;
    for (st = 0; st < DS_COUNT; st++)
        for (n = 0; n < 40u; n++) {
            uint32_t len;
            setup();
            song.g[G_DSTYLE] = (int16_t)st;
            song.g[G_DDENS] = (int16_t)(n * 3u % 128u);
            song.g[G_DWILD] = (int16_t)(n * 7u % 128u);
            dice_roll(DT_BEAT);
            for (i = 0; i < 16u; i += 4u)
                kick &= on(i, DL_KICK);
            for (i = 0; i < 16u; i++)
                rmbl &= !(on(i, DL_KICK2) && on(i, DL_KICK));
            for (i = 2; i < 16u; i += 4u)
                open &= st == DS_HARDGROOVE ? (i == 6u || i == 14u) == on(i, DL_OPEN) : on(i, DL_OPEN);
            len = dg_len(DG_PERC);
            plen &= len == 16u || len == 12u || len == 13u || len == 15u;
            odd += len != 16u;
            if (len == 16u && (st == DS_ACID || st == DS_HARDGROOVE || st == DS_INDUSTRIAL || st == DS_ROLLING))
                clap &= on(4, DL_CLAP) && on(12, DL_CLAP);
        }
    check(kick, "BEAT, 6 styles x 40 rolls: the kick on 1, 2, 3, 4 every time");
    check(rmbl, "  KICK 2 (the rumble) never on a kick");
    check(open, "  the open hat on every off-beat (HARDGROOVE: two of them, a ride)");
    check(plen && odd > 10u, "  PERC 16 steps or an odd 12 / 13 / 15 (some odd ones: polymeter)");
    check(clap, "  a clap on 2 and 4 where the style has one");
}

static void t_seed(void)
{
    static dstep_t a[NSTEP];
    uint32_t i, k, distinct = 0, seen[20];
    setup();
    song.g[G_DSTYLE] = DS_HYPNOTIC;
    dice_roll(DT_BEAT);
    memcpy(a, TDRUM->dstep, sizeof a);
    dice_roll(DT_BEAT);
    dice_step(-1);
    check(!memcmp(a, TDRUM->dstep, sizeof a), "SEED: OCT- back to a roll gives its pattern again, step for step");
    for (k = 0; k < 20u; k++) {
        uint32_t h = 2166136261u;
        const uint8_t *b = (const uint8_t *)TDRUM->dstep;
        dice_roll(DT_BEAT);
        for (i = 0; i < sizeof TDRUM->dstep; i++)
            h = (h ^ b[i]) * 16777619u;
        for (i = 0; i < distinct && seen[i] != h; i++)
            ;
        if (i == distinct)
            seen[distinct++] = h;
    }
    check(distinct >= 15u, "SEED: 20 rolls, 15 or more different beats");
    printf("dice:   %u of 20 different\n", distinct);
}

static void t_acid(void)
{
    uint32_t n, i, inscale = 1, first = 1, tonic_most = 1, acc_w = 0, sl_w = 0, acc_0 = 0, notes = 0;
    for (n = 0; n < 60u; n++) {
        uint32_t cnt[12] = {0}, best = 0, k, mask;
        setup();
        trk[0].p[P_ROOT] = (int16_t)(n % 12u);
        trk[0].p[P_SCALE] = 2;                          /* MIN */
        song.g[G_DSTYLE] = (int16_t)(n % DS_COUNT);
        song.g[G_DWILD] = n < 30u ? 110 : 0;
        dice_roll(DT_T1);
        mask = scale_mask(&trk[0]);
        first &= trk[0].step[0].time == ST_NOTE && trk[0].step[0].note[0] % 12u == n % 12u;
        for (i = 0; i < 16u; i++) {
            const step_t *s = &trk[0].step[i];
            if (s->time != ST_NOTE)
                continue;
            k = (s->note[0] + 12u - n % 12u) % 12u;
            inscale &= (mask >> k) & 1u;
            cnt[k]++;
            notes++;
            if (n < 30u)
                acc_w += (s->flags & SF_ACCENT) != 0, sl_w += (s->flags & SF_SLIDE) != 0;
            else
                acc_0 += (s->flags & SF_ACCENT) != 0;
        }
        for (k = 1; k < 12u; k++)
            best = cnt[k] > best ? cnt[k] : best;
        tonic_most &= cnt[0] >= best;
    }
    check(first && inscale, "ACID: 60 lines, 12 keys: step 1 the tonic, every note in the scale");
    check(tonic_most, "  the tonic the most played note of every line (hypnotic)");
    check(acc_w > acc_0 * 3u / 2u && sl_w > 20u, "  WILD 110: accents (more than at WILD 0) and slides");
    printf("dice:   %u notes; accents %u (WILD 110) / %u (WILD 0), slides %u\n", notes, acc_w, acc_0, sl_w);
}

static void t_tracks(void)
{
    uint32_t i, bass = 1, drone = 1;
    setup();
    host_preset(&trk[1], 0, 2);                        /* (ANALOG SUB BASS) */
    dice_roll(DT_T1 + 1u);
    for (i = 2; i < 16u; i += 4u)
        bass &= trk[1].step[i].time == ST_NOTE;
    for (i = 0; i < 16u; i += 4u)
        bass &= trk[1].step[i].time != ST_NOTE;      /* (not on the kick) */
    check(bass, "TRACK 2: an off-beat bass (every off-beat, none on the beat)");
    setup();
    song.g[G_DWILD] = 0;
    dice_roll(DT_T1 + 3u);
    drone = trk[3].step[0].time == ST_NOTE && trk[3].step[0].n == 2u;
    for (i = 1; i < 16u; i++)
        drone &= trk[3].step[i].time == ST_TIE;
    check(drone, "TRACK 4: a drone (two notes on step 1, held over the 16 steps)");
}

static void t_history(void)
{
    uint32_t k;
    uint16_t s1, s2;
    setup();
    dice_roll(DT_HATS);
    s1 = dice_hist[dice_at].seed;
    dice_roll(DT_HATS);
    s2 = dice_hist[dice_at].seed;
    dice_step(-1);
    check(dice_hist[dice_at].seed == s1 && dice_at == 0u, "HISTORY: OCT- the roll before");
    dice_step(1);
    check(dice_hist[dice_at].seed == s2 && dice_n == 2u, "  OCT+ the one after");
    dice_step(1);
    check(dice_n == 3u && dice_at == 2u && dice_hist[2].target == DT_HATS, "  OCT+ past the newest: a new roll of the same target");
    for (k = 0; k < 30u; k++)
        dice_roll(DT_KICK);
    check(dice_n == DH_N && dice_at == DH_N - 1u, "  30 more: the last 16 kept");
}

static void t_undo(void)
{
    static step_t before[NSTEP];
    setup();
    trk[0].step[3].time = ST_NOTE, trk[0].step[3].n = 1, trk[0].step[3].note[0] = 61;
    memcpy(before, trk[0].step, sizeof before);
    dice_roll(DT_T1);
    check(memcmp(before, trk[0].step, sizeof before) != 0, "UNDO: a roll changes the steps ..");
    check(undo.valid && undo.trk == 0u && !memcmp(before, undo.st, sizeof before),
          "  .. and it is one undo (the steps before it kept: EDIT + OCT- brings them back, ui_pages_test)");
}

static void t_listen(void)
{
    FILE *w = fopen("build/host/dice-beat.wav", "wb");
    int32_t o[2 * CTL];
    uint32_t f, i, len = FS * 8u / CTL * CTL, nz = 0;
    setup();
    song.g[G_BPM] = 132;
    song.g[G_DSTYLE] = DS_HYPNOTIC;
    TDRUM->p[P_E0] = DRUM_SAMPLED + 33u;              /* (BERLIN) */
    dice_roll(DT_BEAT);
    dice_roll(DT_T1);
    transport_req = 1;
    if (w)
        wav_hdr(w, len);
    for (f = 0; f < len; f += CTL) {
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++) {
            nz += o[2u * i] != 0;
            if (w)
                wav_put(w, o[2u * i], o[2u * i + 1u]);
        }
    }
    if (w)
        fclose(w);
    seq_stop();
    check(nz > len / 2u, "LISTEN: a rolled HYPNOTIC beat (BERLIN) with an acid line plays (build/host/dice-beat.wav)");
}

int main(void)
{
    t_beat();
    t_seed();
    t_acid();
    t_tracks();
    t_history();
    t_undo();
    t_listen();
    printf("dice: %s\n", fails ? "FAILED" : "all checks ok");
    return fails;
}
