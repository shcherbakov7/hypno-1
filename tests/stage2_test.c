/* SPDX-License-Identifier: GPL-3.0-only */
/* HYPNO-1 stage 2: the KICK page (drums.c drum_synth_on), the RUMBLE (fx.c rumble_render) and the PUMP sidechain
 * (fx.c duck_block), measured on the mix:
 *   kick     TUNE +12 doubles the kick's pitch (zero crossings), BODY +63 makes its tail longer
 *   rumble   on: a low hum after the kick (what it adds 0.15 .. 0.5 s after it, below ~250 Hz); back to 0 it is
 *            silent and cleared (LEVEL 0 as without it: regress.c's golden renders)
 *   pump     a held synth note dips after each kick by its part's depth (PUMP > ACID), not with depth 0; BEAT pumps
 *            with no drum playing; HATS: only the hats' hits
 * Writes build/host/stage2-*.wav to listen. Exit status: the number of failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main

#define SEC(x) ((uint32_t)((x) * FS))
static int32_t buf[3 * FS];
static int fails;
static void check(int ok, const char *what)
{
    printf("stage2: %-78s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}
static void reset(void)
{
    if (song.playing)
        seq_stop();
    clk_beat = clk_pos = 0;
    host_tracks_init();
    memset(&drums, 0, sizeof drums);
    drums.set = -2;
    TDRUM->p[P_E0] = DRUM_SAMPLED;                       /* the 808: a synthesised kit */
    memset(&rb, 0, sizeof rb);
    memset(rb_mem, 0, sizeof rb_mem);
    memset(&duck, 0, sizeof duck);
    duck.t = 0xFFFFFFFFu;
    rng_state = 0x1234567u;                              /* (the noises' seed: two renders the same) */
}
/* `len` samples of the left channel into buf; a kick at 0 (and every `every` samples, 0: once) */
static void render(uint32_t len, uint32_t every)
{
    int32_t o[2 * CTL];
    uint32_t f, i;
    for (f = 0; f < len; f += CTL) {
        if (f == 0 || (every && f % every < CTL))
            trk_note_on(TDRUM, 36, 110);
        mix_block(o, CTL);
        for (i = 0; i < CTL && f + i < len; i++)
            buf[f + i] = o[2u * i];
    }
}
static void wav(const char *name, uint32_t len)
{
    char path[256];
    FILE *w;
    uint32_t i;
    snprintf(path, sizeof path, "build/host/stage2-%s.wav", name);
    if (!(w = fopen(path, "wb")))
        return;
    wav_hdr(w, len);
    for (i = 0; i < len; i++)
        wav_put(w, buf[i], buf[i]);
    fclose(w);
}
static double energy(uint32_t a, uint32_t b)
{
    double e = 0;
    uint32_t i;
    for (i = a; i < b; i++)
        e += (double)buf[i] * buf[i];
    return e / (double)(b - a);
}
static uint32_t crossings(uint32_t a, uint32_t b)
{
    uint32_t i, n = 0;
    for (i = a + 1u; i < b; i++)
        n += (buf[i - 1] < 0) != (buf[i] < 0);
    return n;
}
/* the energy of the difference x[i] - x[i-1]: high frequencies, relative to the energy (a low hum: small) */
static double brightness(uint32_t a, uint32_t b)
{
    double d = 0;
    uint32_t i;
    for (i = a + 1u; i < b; i++)
        d += (double)(buf[i] - buf[i - 1]) * (buf[i] - buf[i - 1]);
    return d / (double)(b - a) / (energy(a, b) + 1.0);
}

static void t_kick(void)
{
    uint32_t c0, c1;
    double t0, t1;
    reset();
    render(SEC(0.6), 0);
    c0 = crossings(SEC(0.03), SEC(0.12)), t0 = energy(SEC(0.25), SEC(0.5));
    reset();
    song.g[G_KTUNE] = 12;
    render(SEC(0.6), 0);
    c1 = crossings(SEC(0.03), SEC(0.12));
    wav("kick-tune12", SEC(0.6));
    check(c1 > c0 * 17u / 10u && c1 < c0 * 23u / 10u, "KICK TUNE +12: the kick an octave up (its zero crossings about twice)");
    reset();
    song.g[G_KBODY] = 63;
    render(SEC(0.6), 0);
    t1 = energy(SEC(0.25), SEC(0.5));
    check(t1 > t0 * 4.0, "KICK BODY +63: a longer tail (4x the energy 0.25 .. 0.5 s after the hit)");
    printf("stage2:   crossings %u -> %u, tail %.0f -> %.0f\n", c0, c1, t0, t1);
}

static void t_rumble(void)
{
    static int32_t dry[SEC(1.0)];
    double e0, e1, b1, ed = 0;
    uint32_t i, same = 1;
    reset();
    render(SEC(1.0), 0);
    memcpy(dry, buf, sizeof dry);
    e0 = energy(SEC(0.15), SEC(0.5));
    reset();
    song.g[G_RBLVL] = 100;
    render(SEC(1.0), 0);
    e1 = energy(SEC(0.15), SEC(0.5));
    b1 = brightness(SEC(0.15), SEC(0.5));
    wav("rumble", SEC(1.0));
    for (i = SEC(0.15); i < SEC(0.5); i++)               /* what the rumble adds */
        ed += (double)(buf[i] - dry[i]) * (buf[i] - dry[i]);
    ed /= (double)(SEC(0.5) - SEC(0.15));
    check(ed > e0 * 2.0, "RUMBLE LEVEL 100: a hum after the kick (what it adds 0.15 .. 0.5 s: 2x the kick's tail)");
    check(b1 < 0.02, "RUMBLE: low (its high-frequency energy under 2 % of it: below ~250 Hz)");
    printf("stage2:   after the kick: %.0f -> %.0f (the rumble %.0f), brightness %.4f\n", e0, e1, ed, b1);
    reset();
    song.g[G_RBLVL] = 100;
    render(SEC(0.3), 0);
    song.g[G_RBLVL] = 0;
    {
        int32_t o[2 * CTL];
        mix_block(o, CTL);
    }
    for (i = 0; i < sizeof rb_mem / sizeof rb_mem[0]; i++)
        same &= rb_mem[i] == 0;
    check(same && !rb.on, "RUMBLE back to 0: off, its lines cleared (it comes back from rest)");
    /* (LEVEL 0 as without it: tests/regress.c, every golden render unchanged with the rumble built in) */
}

/* the dip of a held note: its level just after a kick over its level just before the next */
static double dip(uint32_t every)
{
    return energy(SEC(0.02), SEC(0.06)) / (energy(every - SEC(0.08), every - SEC(0.02)) + 1.0);
}
static void t_pump(void)
{
    const uint32_t beat = SEC(0.5);
    double d0, d1, dbeat, dhats;
    reset();
    host_preset(&trk[0], 0, 0);
    trk[0].p[P_SUS] = 127;
    trk_note_on(&trk[0], 48, 100);
    song.g[G_DRLVL] = 0;                                 /* (the kick silent: the note alone is measured) */
    render(beat * 2u, beat);
    d0 = dip(beat);
    reset();
    host_preset(&trk[0], 0, 0);
    trk[0].p[P_SUS] = 127;
    trk_note_on(&trk[0], 48, 100);
    song.g[G_DRLVL] = 0;
    song.g[G_SC1] = 127;
    render(beat * 2u, beat);
    d1 = dip(beat);
    wav("pump", beat * 2u);
    check(d0 > 0.8 && d1 < 0.25, "PUMP > ACID 127: the held note dips after the kick (a silent kick: a ghost); 0: not");
    printf("stage2:   dip (after / before): %.2f -> %.2f\n", d0, d1);
    reset();
    host_preset(&trk[0], 0, 0);
    trk[0].p[P_SUS] = 127;
    trk_note_on(&trk[0], 48, 100);
    song.g[G_SC1] = 127;
    song.g[G_SCSRC] = 4;                                 /* BEAT */
    transport_req = 1;
    {
        int32_t o[2 * CTL];
        uint32_t f, i;
        for (f = 0; f < beat * 2u; f += CTL) {           /* no drum at all */
            mix_block(o, CTL);
            for (i = 0; i < CTL && f + i < beat * 2u; i++)
                buf[f + i] = o[2u * i];
        }
    }
    dbeat = energy(beat + SEC(0.02), beat + SEC(0.06)) / (energy(beat - SEC(0.08), beat - SEC(0.02)) + 1.0);
    check(dbeat < 0.25, "PUMP SRC BEAT: every beat dips it with no drum playing");
    reset();
    host_preset(&trk[0], 0, 0);
    trk[0].p[P_SUS] = 127;
    trk_note_on(&trk[0], 48, 100);
    song.g[G_DRLVL] = 0;
    song.g[G_SC1] = 127;
    song.g[G_SCSRC] = DG_HATS;
    render(beat * 2u, beat);                             /* kicks only: the hats' source does not move */
    dhats = dip(beat);
    check(dhats > 0.8, "PUMP SRC HATS: kicks alone do not dip it");
    printf("stage2:   BEAT %.2f, HATS on kicks %.2f\n", dbeat, dhats);
}

int main(void)
{
    t_kick();
    t_rumble();
    t_pump();
    printf("stage2: %s\n", fails ? "FAILED" : "all checks ok");
    return fails;
}
