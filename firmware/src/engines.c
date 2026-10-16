/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Engine table: order = PRESETS browsing order (and the engine numbers of the editor protocol). */
#include "dsp.c"
/* SLOOP 2.5: the big per-part state of GRAIN, FM6 and PHYS shares one arena per synth part (a part plays one
 * engine at a time: on a switch the old engine fades out, its voices end, then the new one starts, voice.c
 * engine_block). The first engine to ask for it after another one gets it cleared, as at power-on. */
static void *eng_arena_of(const track_t *t, uint32_t eng);
static inline int32_t voice_amp(int32_t s, const vmod_t *m, uint32_t i) { return mulq15(mulq15(s, amp_at(m, i)), VOICE_FS); }
static inline int32_t soft_knee(int32_t y, int32_t k)   /* linear up to k, then only the peaks saturate */
{
    int32_t a = y < 0 ? -y : y;
    if (a <= k)
        return y;
    a = k + (softclip((a - k) * 2) >> 1);
    return y < 0 ? -a : a;
}
/* (pitch_inc: felucca_tables.h, SLOOP 2.5: from the top octave) */
#include "eng_analog.c"
#include "eng_digital.c"
#include "eng_phase.c"
#include "eng_lofi.c"
#include "eng_sample.c"
#include "eng_formant.c"
#include "eng_trio.c"
#include "eng_fm6.c"            /* FM6: 6-operator FM, msfa ported (fm6_core.c, Apache-2.0); SLOOP 2.4 */
#include "eng_noise.c"          /* NOISE: from Felucca 1.0; SLOOP 2.5 */
#include "eng_acid.c"           /* HYPNO-1 ACID: the 303, in WHEEL's place (7) */

/* HYPNO-1: GRAIN (eng_grain.c) and PHYS (eng_phys.c) are not built: their state took 7.1 and 8.6 KB of the arena a
 * part, FM6 needs 1.8, and the RAM goes to the fourth synth part; nor is WHEEL (eng_drawbar.c, 2.9 KB of RAM for its
 * voices). Their engine numbers stay (ENGINES[]): a project or user preset that has one plays ANALOG (eng_live) */
typedef union {
    fm6_note_t fm6[FM6_POLY];
} eng_arena_t;
static eng_arena_t eng_arena[NPART] __attribute__((section(".pool")));
static uint8_t eng_arena_own[NPART];            /* the engine + 1 whose state is in it, 0 = none */
#ifdef ARENA_STATS
static uint32_t eng_arena_claims, eng_arena_bad;   /* host tests: claims, accesses by an engine the part does not play */
#endif
static void *eng_arena_of(const track_t *t, uint32_t eng)
{
    uint32_t p = (uint32_t)(t - trk);
    if (p >= NPART)
        return 0;
#ifdef ARENA_STATS
    if (t->engine != eng)
        eng_arena_bad++;
#endif
    if (eng_arena_own[p] != eng + 1u) {
        memset(&eng_arena[p], 0, sizeof eng_arena[p]);
        eng_arena_own[p] = (uint8_t)(eng + 1u);
#ifdef ARENA_STATS
        eng_arena_claims++;
#endif
    }
    return &eng_arena[p];
}
#if FELUCCA_SLICE
#include "eng_slice.c"
#endif

static const engine_t *const ENGINES[NENGINES] = {&ENG_ANALOG, &ENG_DIGITAL, &ENG_PHASE, &ENG_LOFI, &ENG_SAMPLE,
                                                    &ENG_FORMANT, &ENG_TRIO,
                                                    &ENG_ACID,   /* 7 (ENGI_ACID; SLOOP's WHEEL, the drawbar organ, retired) */
                                                    &ENG_ANALOG, /* 8 (ENGI_GRAIN): retired in HYPNO-1 */
                                                    &ENG_FM6,    /* 9 (ENGI_FM6): always; SLICE after it (core.h) */
                                                    &ENG_ANALOG, /* 10 (ENGI_PHYS): retired in HYPNO-1 */
                                                    &ENG_NOISE,
#if FELUCCA_SLICE
                                                    &ENG_SLICE,
#endif
};
_Static_assert(ENGI_FM6 == 9u, "ENGINES[ENGI_FM6] is FM6");
_Static_assert(ENGI_GRAIN == 8u && ENGI_PHYS == 10u && ENGI_NOISE == 11u && ENGI_SLICE == 12u, "ENGINES[] order");
static int eng_retired(uint32_t e) { return e == ENGI_GRAIN || e == ENGI_PHYS; }
/* an engine number from a project, a user preset or the editor: a retired one plays ANALOG */
static uint32_t eng_live(uint32_t e) { return eng_retired(e % NENGINES) ? 0u : e % NENGINES; }

/* every factory sound as loud as the others: a level trim per preset, 1/2 dB, measured on a phrase
 * that fits the sound (tools/level_presets.py writes preset_trim.h); a track keeps it in P_ED_FX */
#include "preset_trim.h"
static int16_t preset_trim(uint32_t e, uint32_t pi)
{
    return e < PT_ENGINES && pi < PT_MAX ? PRESET_TRIM[e][pi] : 0;
}
