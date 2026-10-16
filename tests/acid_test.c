/* SPDX-License-Identifier: GPL-3.0-only */
/* HYPNO-1 stage 3: the ACID engine (eng_acid.c), measured on track 1's own output (track_render) and, for the
 * riff, through the sequencer and the mix:
 *   RES      the resonance rings: more energy near the cutoff (the high-frequency share up, at a low cutoff)
 *   ENV/DEC  the filter envelope: a note starts bright and closes (DEC short: sooner)
 *   ACC      an accented note (velocity 127) is louder and brighter; accents in a row climb (the sweep)
 *   SLIDE    a legato note with the slide glides (no jump), the envelope does not start over
 *   WAVE     the square has no even harmonics to speak of (its second harmonic far below the saw's)
 *   limits   SQL, RES, DRV at 127 over the whole keyboard: bounded, silent after the release; no denormal-ish
 *            stuck DC
 *   SEQ      a 16-step line with ACC and SLIDE steps plays (the sequencer's accent is velocity 127)
 * Writes build/host/acid-*.wav. Exit status: the number of failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <math.h>

#define SEC(x) ((uint32_t)((x) * FS) / CTL * CTL)
static int32_t buf[4 * FS];
static int fails;
static void check(int ok, const char *what)
{
    printf("acid: %-82s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}
static track_t *T;
static void setup(uint32_t preset)
{
    host_tracks_init();
    memset(acid_sweep, 0, sizeof acid_sweep);
    T = &trk[0];
    memset(T->v, 0, sizeof T->v);
    host_preset(T, ENGI_ACID, preset);
    T->p[P_TRANS] = 0;
}
/* `len` samples of the track alone into buf, from `at` */
static void run(uint32_t at, uint32_t len)
{
    int32_t o[CTL];
    uint32_t f, i;
    for (f = 0; f < len; f += CTL) {
        track_render(T, o, CTL);
        for (i = 0; i < CTL; i++)
            buf[at + f + i] = o[i];
    }
}
static double energy(uint32_t a, uint32_t b)
{
    double e = 0;
    uint32_t i;
    for (i = a; i < b; i++)
        e += (double)buf[i] * buf[i];
    return e / (double)(b - a);
}
static double bright(uint32_t a, uint32_t b)   /* the energy of x[i] - x[i-1] over the energy: the high share */
{
    double d = 0;
    uint32_t i;
    for (i = a + 1u; i < b; i++)
        d += (double)(buf[i] - buf[i - 1]) * (buf[i] - buf[i - 1]);
    return d / (double)(b - a) / (energy(a, b) + 1.0);
}
static uint32_t crossings(uint32_t a, uint32_t b)
{
    uint32_t i, n = 0;
    for (i = a + 1u; i < b; i++)
        n += (buf[i - 1] < 0) != (buf[i] < 0);
    return n;
}
static double harm(uint32_t a, uint32_t b, double hz)   /* the amplitude of one frequency (a DFT bin) */
{
    double re = 0, im = 0;
    uint32_t i;
    for (i = a; i < b; i++) {
        re += buf[i] * cos(2 * M_PI * hz * i / FS);
        im += buf[i] * sin(2 * M_PI * hz * i / FS);
    }
    return sqrt(re * re + im * im) / (double)(b - a);
}
static void wav(const char *name, uint32_t len)
{
    char path[256];
    FILE *w;
    uint32_t i;
    snprintf(path, sizeof path, "build/host/acid-%s.wav", name);
    if (!(w = fopen(path, "wb")))
        return;
    wav_hdr(w, len);
    for (i = 0; i < len; i++)
        wav_put(w, buf[i], buf[i]);
    fclose(w);
}
/* one held note, `len` long; returns the brightness of [a, b) */
static double note_bright(int res, int cut, uint32_t a, uint32_t b)
{
    setup(0);
    T->p[P_E1] = (int16_t)cut, T->p[P_E2] = (int16_t)res, T->p[P_E3] = 0, T->p[P_E6] = 0;
    trk_note_on(T, 36, 100);
    run(0, b);
    return bright(a, b);
}

static void t_filter(void)
{
    double b0 = note_bright(0, 30, SEC(0.1), SEC(0.4)), b1 = note_bright(120, 30, SEC(0.1), SEC(0.4)), e0, e1;
    check(b1 > b0 * 1.6, "RES 120: the resonance rings (the high share of a low-cut saw up 1.6x)");
    printf("acid:   brightness RES 0 %.5f, RES 120 %.5f\n", b0, b1);
    setup(0);
    T->p[P_E1] = 20, T->p[P_E3] = 110, T->p[P_E4] = 10;
    trk_note_on(T, 36, 100);
    run(0, SEC(0.6));
    e0 = bright(0, SEC(0.03)), e1 = bright(SEC(0.4), SEC(0.6));
    check(e0 > e1 * 3.0, "ENV 110, DEC 10: the note starts bright and closes (3x the high share)");
    printf("acid:   brightness at the start %.5f, at 0.4 s %.5f\n", e0, e1);
}

static void t_accent(void)
{
    double ln, la, bn, ba, s1, s4;
    uint32_t k, at = 0;
    setup(0);
    trk_note_on(T, 36, 100);
    run(0, SEC(0.2));
    ln = energy(0, SEC(0.2));
    setup(0);
    T->p[P_E5] = 0;                                   /* an accent with ACC 0: no sweep */
    trk_note_on(T, 36, 127);
    run(0, SEC(0.2));
    bn = bright(SEC(0.015), SEC(0.06));
    setup(0);
    T->p[P_E5] = 127;
    trk_note_on(T, 36, 127);
    run(0, SEC(0.2));
    la = energy(0, SEC(0.2)), ba = bright(SEC(0.015), SEC(0.06));
    check(la > ln * 1.8 && ba > bn * 1.3, "ACC: velocity 127 louder than 100 (energy 1.8x); ACC 127 sweeps it (1.3x the ACC 0 high share)");
    printf("acid:   energy %.0f -> %.0f, brightness %.5f -> %.5f\n", ln, la, bn, ba);
    setup(0);                                         /* four accents in a row, 1/16 at 130 BPM apart */
    for (k = 0; k < 4u; k++) {
        trk_note_on(T, 36, 127);
        run(at, SEC(0.08));
        trk_note_off(T, 36);
        run(at + SEC(0.08), SEC(0.035));
        if (!k)
            s1 = bright(SEC(0.005), SEC(0.04));
        at += SEC(0.115);
    }
    s4 = bright(at - SEC(0.115) + SEC(0.005), at - SEC(0.115) + SEC(0.04));
    check(s4 > s1 * 1.1, "ACC in a row: the sweep climbs (the 4th accent brighter than the 1st)");
    printf("acid:   1st accent %.5f, 4th %.5f\n", s1, s4);
}

static void t_slide(void)
{
    uint32_t c0, c1, c2;
    double b0, b1;
    setup(0);
    T->p[P_E1] = 70, T->p[P_E2] = 40, T->p[P_E4] = 20;
    trk_note_on(T, 36, 100);
    run(0, SEC(0.3));
    b0 = bright(SEC(0.25), SEC(0.3));
    T->slide_glide = 1;                                /* the sequencer: the last step slides into this one */
    trk_note_on(T, 48, 100);
    trk_note_off(T, 36);
    run(SEC(0.3), SEC(0.3));
    c0 = crossings(SEC(0.2), SEC(0.3)), c1 = crossings(SEC(0.3), SEC(0.32)) * 5u, c2 = crossings(SEC(0.5), SEC(0.6));
    b1 = bright(SEC(0.3), SEC(0.33));
    check(c2 > c0 * 17u / 10u && c1 < c2 * 9u / 10u, "SLIDE: an octave up, gliding (the first 20 ms below the new pitch)");
    check(b1 < b0 * 2.5, "SLIDE: no new envelope (no fresh filter sweep at the slide)");
    printf("acid:   crossings / 0.1 s: before %u, first 20 ms %u, after %u; brightness %.5f -> %.5f\n", c0, c1, c2, b0, b1);
    wav("slide", SEC(0.6));
}

static void t_dc(void)
{
    double dc = 0, e;
    uint32_t i;
    setup(0);
    trk_note_on(T, 26, 127);
    run(0, SEC(1.0));
    for (i = SEC(0.2); i < SEC(1.0); i++)
        dc += buf[i];
    dc /= (double)(SEC(1.0) - SEC(0.2));
    e = sqrt(energy(SEC(0.2), SEC(1.0)));
    check(fabs(dc) < e * 0.02, "DC: a held low note has no offset to speak of (under 2 % of its RMS)");
    printf("acid:   DC %.1f, RMS %.1f\n", dc, e);
}

static void t_wave(void)
{
    double s2, s1, q2, q1;
    setup(0);
    T->p[P_E1] = 127, T->p[P_E2] = 0, T->p[P_E3] = 0, T->p[P_E6] = 0;
    trk_note_on(T, 45, 100);                           /* A2, 110 Hz */
    run(0, SEC(0.5));
    s1 = harm(SEC(0.1), SEC(0.5), 110), s2 = harm(SEC(0.1), SEC(0.5), 220);
    setup(0);
    T->p[P_E0] = 127, T->p[P_E1] = 127, T->p[P_E2] = 0, T->p[P_E3] = 0, T->p[P_E6] = 0;
    trk_note_on(T, 45, 100);
    run(0, SEC(0.5));
    q1 = harm(SEC(0.1), SEC(0.5), 110), q2 = harm(SEC(0.1), SEC(0.5), 220);
    check(s2 > s1 * 0.3 && q2 < q1 * 0.1, "WAVE: saw (2nd harmonic ~1/2) -> square (2nd harmonic under 1/10)");
    printf("acid:   saw h2/h1 %.3f, square h2/h1 %.3f\n", s2 / s1, q2 / q1);
}

static void t_limits(void)
{
    uint32_t n, i, peak = 0, after = 0;
    setup(0);
    T->p[P_E2] = 127, T->p[P_E6] = 127, T->p[P_E7] = 127, T->p[P_E3] = 127;
    for (n = 24; n <= 96; n += 6) {
        trk_note_on(T, n, 127);
        run(0, SEC(0.1));
        for (i = 0; i < SEC(0.1); i++)
            if ((uint32_t)abs(buf[i]) > peak)
                peak = (uint32_t)abs(buf[i]);
        trk_note_off(T, n);
    }
    run(0, SEC(0.5));
    for (i = SEC(0.4); i < SEC(0.5); i++)
        if ((uint32_t)abs(buf[i]) > after)
            after = (uint32_t)abs(buf[i]);
    check(peak > 2000u && peak < 300000u && after < 8u, "SQL RES DRV ENV 127, C1 .. C7: bounded, silent after the release");
    printf("acid:   peak %u, after the release %u\n", peak, after);
}

/* a hypnotic 16-step line (C, its octave, the minor third and the fifth), ACC and SLIDE steps */
static void t_seq(void)
{
    static const uint8_t N[16] = {36, 36, 48, 36, 39, 36, 43, 48, 36, 36, 46, 36, 39, 48, 36, 43};
    static const uint8_t F[16] = {1, 0, 2, 0, 1, 0, 2, 0, 0, 1, 2, 0, 1, 0, 2, 1};
    int32_t o[2 * CTL];
    uint32_t i, f, nz = 0, len = SEC(3.6);
    setup(1);                                          /* SQUELCH */
    song.g[G_BPM] = 133;
    T->p[P_SLEN] = 16;
    for (i = 0; i < 16u; i++)
        put_step(T, i, 1, &N[i], ST_NOTE, F[i]);
    T->seq_active = 1;
    transport_req = 1;
    for (f = 0; f < len; f += CTL) {
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++)
            buf[f + i] = o[2u * i], nz += o[2u * i] != 0;
    }
    seq_stop();
    wav("riff", len);
    check(nz > len / 2u, "SEQ: a 16-step line with ACC and SLIDE plays (build/host/acid-riff.wav)");
    printf("acid:   riff energy %.0f\n", energy(0, len));
}

int main(void)
{
    t_filter();
    t_accent();
    t_slide();
    t_dc();
    t_wave();
    t_limits();
    t_seq();
    printf("acid: %s\n", fails ? "FAILED" : "all checks ok");
    return fails;
}
