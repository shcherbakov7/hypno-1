/* SPDX-License-Identifier: GPL-3.0-only */
/* HYPNO-1 ACID: a TB-303 voice in fixed point (after the TB-303's circuit as Open303 models it):
 *   osc     a band-limited saw, blended into a square (WAVE)
 *   filter  four one-pole low-passes in a ladder, the resonance fed back from the last one through a
 *           high-pass (~110 Hz, as the 303's): the bass stays at any RES; a soft clip at the ladder's input
 *           keeps the self-oscillation round. Its cutoff: CUT, the filter envelope (ENV), the accent sweep
 *   MEG     the filter envelope: instant attack, an exponential decay (DEC; an accented note: always short)
 *   accent  a note at velocity >= 120 (a step's ACC plays at 127): louder, and its envelope charges the
 *           accent sweep, a capacitor that discharges slowly: accents in a row climb (the "wow")
 *   VEG     the amplitude: the ADSR as a gate (instant on, a short release) times a slow decay
 *   DRV     a drive after the filter; SQL (squelch): one knob opening CUT, RES, ENV, ACC and DRV together
 * Slides are the sequencer's: SLIDE on a step makes the next note legato (no new envelope) with a glide.
 * State: s[0..3] the ladder (Q19), s[4] the MEG (Q15), s[5] the VEG (Q15), s[6] the feedback high-pass,
 * ph[0] the oscillator, ph[2] the last block's filter coefficient. Cost: ~45 ops a sample, 4 of them 32x32->64 */
#define ACID_ACC_VEL 120u                              /* a velocity this high is an accent */
static int32_t acid_sweep[NPART];                      /* the accent sweep of each part (Q15) */

static uint32_t acid_k32(uint32_t idx)                 /* DECAY_K (per sample, Q16) -> per block of 32 */
{
    uint32_t k = DECAY_K[idx & 127u], i;
    for (i = 0; i < 5u; i++)
        k = (k * k) >> 16;
    return k;
}
static int32_t acid_q(int32_t v) { return v > 127 ? 127 : v; }
/* the knobs with SQUELCH in: CUT RES ENV DEC ACC DRV */
static void acid_knobs(const int16_t *p, int32_t k[6])
{
    int32_t sq = p[P_E7];
    k[0] = acid_q(p[P_E1] + sq * 24 / 127);
    k[1] = acid_q(p[P_E2] + sq * 30 / 127);
    k[2] = acid_q(p[P_E3] + sq * 56 / 127);
    k[3] = p[P_E4];
    k[4] = acid_q(p[P_E5] + sq * 34 / 127);
    k[5] = acid_q(p[P_E6] + sq * 40 / 127);
}
/* the cutoff (1/16 semitones) -> the one-pole's g = w / (1 + w), Q15 */
static int32_t acid_g(int32_t n16)
{
    uint32_t inc = pitch_inc(clamp(n16, 0, 2047));
    uint32_t w = (uint32_t)(((uint64_t)(inc >> 17) * 205887u) >> 15);   /* 2 pi f / fs, Q15 */
    uint32_t g = (w << 15) / (32768u + w);
    return (int32_t)(g > 24000u ? 24000u : g);
}

static void acid_note_on(track_t *t, voice_t *v)
{
    (void)t;
    if (!v->env && !v->env_out) {                      /* a fresh note (not a retrigger): a clean start */
        v->ph[0] = 0;
        v->s[0] = v->s[1] = v->s[2] = v->s[3] = v->s[6] = 0;
        v->ph[2] = 0;
    }
    v->s[4] = 32767;                                   /* the MEG and the VEG start over */
    v->s[5] = 32767;
}

static int32_t acid_amp(track_t *t, voice_t *v, int32_t adsr)
{
    int32_t k[6], g;
    acid_knobs(t->p, k);
    v->s[5] = (int32_t)(((int64_t)v->s[5] * acid_k32(116)) >> 16);   /* the VEG: ~2.5 s */
    g = v->vel >= ACID_ACC_VEL ? 32767 : 32767 - k[4] * 110;           /* not accented: up to ~5 dB lower */
    return mulq15(mulq15(adsr, v->s[5]), g);
}

static void acid_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    int32_t k[6], acc = v->vel >= ACID_ACC_VEL, i;
    int32_t meg = v->s[4], n16, g0, g1, fbk, drv, wv = p[P_E0] * 258;
    uint32_t ph = v->ph[0], inc = m->inc, part = (uint32_t)(t - trk);
    int32_t s0 = v->s[0], s1 = v->s[1], s2 = v->s[2], s3 = v->s[3], hp = v->s[6];
    acid_knobs(p, k);
    if (part < NPART && v == &t->v[0]) {               /* the accent sweep: charges fast, discharges slowly */
        int32_t want = acc ? (meg * k[4]) >> 7 : 0, *sw = &acid_sweep[part];
        *sw += (int32_t)(((int64_t)(want - *sw) * (want > *sw ? 1100 : 80)) >> 15);   /* ~30 ms up, ~300 down */
    }
    meg = (int32_t)(((int64_t)meg * acid_k32(acc ? 64u : 70u + (uint32_t)k[3] * 48u / 127u)) >> 16);
    v->s[4] = meg;
    /* the cutoff: CUT (147 Hz .. 2.6 kHz, as the 303's knob; an octave lower with all of ENV), the MEG (up to
     * 5 octaves over it), the accent sweep (up to 5) */
    n16 = 50 * 16 + (((k[0] << 8) + m->cutoff) * 800) / 32512 - k[2] * 192 / 127 +
          ((((meg * k[2]) >> 7) * 960) >> 15) + ((part < NPART ? acid_sweep[part] : 0) * 960 >> 15);
    g1 = acid_g(n16);
    g0 = v->ph[2] ? (int32_t)v->ph[2] : g1;
    v->ph[2] = (uint32_t)g1;
    fbk = k[1] * 16200 / 127;                          /* the resonance, Q12: 0 .. ~4 (self-oscillates near the top) */
    drv = 4096 + k[5] * 100;                           /* the drive, Q12: 1x .. ~4x */
    for (i = 0; i < (int32_t)n; i++) {
        int32_t g = g0 + (((g1 - g0) * i) >> CTL_LOG2), x, u, y;
        x = osc_saw(ph, inc);
        if (wv)                                        /* WAVE: the saw into the square */
            x += mulq15(osc_pulse(ph, inc, 0x80000000u) * 2 - x, wv);
        ph += inc;
        u = softclip((x >> 1) - ((fbk * ((s3 - hp) >> 4)) >> 12)) << 4;
        s0 += (int32_t)(((int64_t)g * (u - s0)) >> 15);
        s1 += (int32_t)(((int64_t)g * (s0 - s1)) >> 15);
        s2 += (int32_t)(((int64_t)g * (s1 - s2)) >> 15);
        s3 += (int32_t)(((int64_t)g * (s2 - s3)) >> 15);
        hp += ((s3 - hp) * 520) >> 15;                 /* ~110 Hz */
        y = softclip(((s3 >> 4) * drv) >> 12);
        out[i] += mulq15(mulq15(y, amp_at(m, (uint32_t)i)), VOICE_FS) << 3;   /* (+12 dB: the ladder takes the most off) */
    }
    v->ph[0] = ph;
    v->s[0] = s0, v->s[1] = s1, v->s[2] = s2, v->s[3] = s3, v->s[6] = hp;
}

/* {WAVE, CUT, RES, ENV, DEC, ACC, DRV, SQL}, {A D S R}, fenv, mono (LEGATO: a SLIDE does not restart it) */
#define ACID_ENV {0, 64, 127, 8}
static const preset_t ACID_PRESETS[] = {
    {"CLASSIC", {0, 40, 96, 70, 50, 80, 20, 0}, ACID_ENV, 0, 1, FX(0, 0, 18, 8), XP(P_TRANS + 1, -12)},
    {"SQUELCH", {0, 30, 116, 96, 40, 100, 40, 40}, ACID_ENV, 0, 1, FX(10, 0, 20, 8), XP(P_TRANS + 1, -12)},
    {"RUBBER", {0, 22, 84, 110, 30, 70, 30, 0}, ACID_ENV, 0, 1, FX(0, 0, 12, 4), XP(P_TRANS + 1, -24)},
    {"SCREAMER", {0, 56, 120, 90, 70, 110, 100, 30}, ACID_ENV, 0, 1, FX(40, 0, 24, 10), XP(P_TRANS + 1, -12)},
    {"DEEP SUB", {0, 14, 70, 60, 60, 50, 30, 0}, ACID_ENV, 0, 1, FX(0, 0, 0, 0), XP(P_TRANS + 1, -24)},
    {"PHUTURE", {0, 34, 110, 80, 90, 90, 30, 0}, ACID_ENV, 0, 1, FX(0, 0, 22, 12), XP(P_TRANS + 1, -12)},
    {"TEKK 145", {0, 46, 104, 100, 34, 120, 80, 20}, ACID_ENV, 0, 1, FX(30, 0, 16, 6), XP(P_TRANS + 1, -12)},
    {"HYPNOTIC", {0, 28, 100, 50, 110, 60, 20, 0}, ACID_ENV, 0, 1, FX(0, 0, 34, 20), XP(P_TRANS + 1, -12)},
    {"WET DUB", {0, 36, 92, 64, 80, 70, 10, 0}, ACID_ENV, 0, 1, FX(0, 0, 50, 40), XP(P_TRANS + 1, -12)},
    {"SQUARE", {127, 36, 100, 80, 50, 90, 30, 0}, ACID_ENV, 0, 1, FX(0, 0, 18, 8), XP(P_TRANS + 1, -12)},
    {"HOLLOW SQ", {127, 20, 116, 60, 90, 70, 10, 0}, ACID_ENV, 0, 1, FX(0, 0, 30, 16), XP(P_TRANS + 1, -24)},
    {"BLEND", {64, 40, 104, 84, 60, 90, 40, 0}, ACID_ENV, 0, 1, FX(0, 0, 18, 8), XP(P_TRANS + 1, -12)},
    {"ACID LEAD", {0, 64, 100, 70, 70, 80, 60, 0}, ACID_ENV, 0, 1, FX(10, 0, 30, 16), XP(P_TRANS + 1, 0)},
    {"DISTORTED", {0, 44, 108, 90, 50, 100, 127, 10}, ACID_ENV, 0, 1, FX(60, 0, 14, 6), XP(P_TRANS + 1, -12)},
    {"SLOW BURN", {0, 18, 112, 120, 127, 60, 30, 0}, ACID_ENV, 0, 1, FX(0, 0, 26, 14), XP(P_TRANS + 1, -12)},
    {"TICK BASS", {0, 26, 60, 90, 10, 60, 40, 0}, ACID_ENV, 0, 1, FX(0, 0, 0, 0), XP(P_TRANS + 1, -24)},
};

static const engine_t ENG_ACID = {
    "ACID", {"303", "303 2"},
    {
        {"WAVE", F_PCT, 0, 127, 0, 0, 0},
        {"CUT", F_PCT, 0, 127, 40, 0, 0},
        {"RES", F_PCT, 0, 127, 96, 0, 0},
        {"ENV", F_PCT, 0, 127, 70, 0, 0},
        {"DEC", F_PCT, 0, 127, 50, 0, 0},
        {"ACC", F_PCT, 0, 127, 80, 0, 0},
        {"DRV", F_PCT, 0, 127, 20, 0, 0},
        {"SQL", F_PCT, 0, 127, 0, 0, 0},
    },
    ACID_PRESETS, sizeof(ACID_PRESETS) / sizeof(ACID_PRESETS[0]), -1, acid_note_on, acid_render,
    0xB7E0, {P_E1, P_E2, P_E3, P_E7}, 0, acid_amp,
};
