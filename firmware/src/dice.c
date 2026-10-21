/* SPDX-License-Identifier: GPL-3.0-only */
/* HYPNO-1 DICE: a techno beat or a track's line from one roll (ARP + SEQ held: ui_layers.c, the DICE layer).
 * Not noise: the rules of the style.
 *   KICK    four to the floor (INDUSTRIAL / ACID: WILD adds a pickup)
 *   RUMBLE  KICK 2 in the kick's gaps: the 16ths between the beats the style rolls on
 *   HATS    the open hat on the off-beats, closed 16ths with an accent pattern (DENS: how many); HARDGROOVE: a shaker
 *           and a ride, INDUSTRIAL: ratchets
 *   PERC    a Euclidean rhythm on the style's sound, a random rotation; HYPNOTIC, DUB, ROLLING: WILD gives the group
 *           an odd length (12, 13, 15): it drifts against the kick. A clap on 2 and 4 where the style has one
 *   synth   the ACID engine (any track that plays it): an acid line in the track's scale (a chromatic one: minor;
 *           HYPNOTIC: phrygian), the tonic most of all, accents, slides and octaves as WILD says. Else by the track:
 *           2 an off-beat bass, 3 Euclidean stabs (a triad of the scale), 4 a drone (a held fifth over 16 steps)
 * A roll is a seed (shown, 4 hex digits) with the style, DENS and WILD: the same roll gives the same pattern. The
 * last 16 are kept: OCT- goes back, OCT+ forward (past the newest: a new roll of the same target). Each roll is one
 * undo (EDIT + OCT-). */
enum { DT_BEAT, DT_KICK, DT_RUMBLE, DT_HATS, DT_PERC, DT_T1, DT_COUNT = DT_T1 + NPART };
enum { DS_HYPNOTIC, DS_ACID, DS_HARDGROOVE, DS_DUB, DS_INDUSTRIAL, DS_ROLLING, DS_COUNT };
static const char *const DICE_TARGET[DT_COUNT] = {"beat", "kick", "rmbl", "hats", "perc", "trk 1", "trk 2", "trk 3", "trk 4"};
enum { DL_KICK = 0, DL_KICK2 = 1, DL_SNARE = 2, DL_CLAP = 3, DL_HAT = 4, DL_OPEN = 5, DL_PEDAL = 6, DL_RIM = 7, DL_SNARE2 = 8,
       DL_TOML = 9, DL_TOMH = 10, DL_CRASH = 11, DL_RIDE = 12, DL_SHAKER = 13, DL_CONGA = 14, DL_BELL = 15 };
_Static_assert(DRUM_LANES == 16, "the DICE lanes are drums.c's");

#define DH_N 16u
typedef struct { uint16_t seed; uint8_t target, style, dens, wild; } dice_roll_t;
static dice_roll_t dice_hist[DH_N];
static uint8_t dice_n, dice_at;                /* rolls kept; the one in force (dice_hist[dice_at]) */
static uint32_t dice_rng, dice_salt = 0x2545F491u;

static uint32_t drnd(void)
{
    dice_rng ^= dice_rng << 13;
    dice_rng ^= dice_rng >> 17;
    dice_rng ^= dice_rng << 5;
    return dice_rng;
}
static uint32_t dr(uint32_t n) { return n ? (drnd() >> 8) % n : 0u; }
static int dchance(int32_t of127) { return (int32_t)dr(128) < of127; }
static int euclid(uint32_t i, uint32_t k, uint32_t n, uint32_t rot) { return ((i + rot) % n * k) % n < k; }

/* ---------------------------------------------------------- the beat --- */
static void dice_lane(dstep_t *d, uint32_t i, uint32_t lane, uint32_t lvl, uint32_t rat)
{
    dstep_set(&d[i % NSTEP], lane, lvl, rat);
}
static void dice_kick(dstep_t *d, uint32_t st, uint32_t wild)
{
    uint32_t i;
    for (i = 0; i < 16u; i += 4u)
        dice_lane(d, i, DL_KICK, i ? LV_NORM : LV_HARD, 0);
    if ((st == DS_INDUSTRIAL || st == DS_ACID) && dchance((int32_t)wild / 2))
        dice_lane(d, dchance(64) ? 14u : 15u, DL_KICK, LV_SOFT, 0);   /* a pickup into the bar */
    dg_set(DG_KICK, 16, (int32_t)dg_level(DG_KICK));
}
static void dice_rumble(dstep_t *d, uint32_t st, uint32_t dens, uint32_t wild)
{
    static const uint8_t AT[DS_COUNT] = {0x4, 0x8, 0x4, 0x4, 0xC, 0xE};   /* the 16ths of a beat it rolls on (bit 1..3) */
    uint32_t b, j, at = AT[st];
    if (st == DS_HYPNOTIC && dens > 64u)
        at |= 0x8;
    if (st == DS_DUB && dens < 80u)
        at = 0;
    for (b = 0; b < 4u; b++) {
        if (b && dchance((int32_t)wild / 4))
            continue;                                  /* WILD: a beat without */
        for (j = 1; j < 4u; j++)
            if (((at >> j) & 1u) && !dstep_has(&d[b * 4u + j], DL_KICK))   /* (in the kick's gaps: not on a pickup) */
                dice_lane(d, b * 4u + j, DL_KICK2, j == 2u ? LV_SOFT : LV_GHOST, 0);
    }
    dg_set(DG_RUMBLE, 16, (int32_t)dg_level(DG_RUMBLE));
}
static void dice_hats(dstep_t *d, uint32_t st, uint32_t dens, uint32_t wild)
{
    static const uint8_t BASE[DS_COUNT] = {70, 110, 60, 30, 80, 110};   /* closed 16ths' share at DENS 64 */
    uint32_t i;
    int32_t p = clamp((int32_t)BASE[st] * (int32_t)(dens + 16u) / 80, 0, 127);
    for (i = 2; i < 16u; i += 4u)                      /* the off-beats: the open hat (HARDGROOVE: two of them, the ride) */
        if (st != DS_HARDGROOVE || i == 6u || i == 14u)
            dice_lane(d, i, DL_OPEN, LV_NORM, 0);
    for (i = 0; i < 16u; i++) {
        uint32_t lv = i % 4u == 3u ? LV_NORM : i % 4u == 1u ? LV_SOFT : LV_GHOST;
        if (i % 4u == 2u || !dchance(p))
            continue;
        if (st == DS_HARDGROOVE)
            dice_lane(d, i, DL_SHAKER, lv, 0);
        else
            dice_lane(d, i, DL_HAT, lv, st == DS_INDUSTRIAL && dchance((int32_t)wild / 3) ? 1u + dr(3) : 0u);
    }
    if (st == DS_HARDGROOVE)
        for (i = 2; i < 16u; i += 4u)
            dice_lane(d, i, DL_RIDE, LV_SOFT, 0);
    if ((st == DS_HYPNOTIC || st == DS_DUB) && dchance((int32_t)wild))
        dice_lane(d, 4u * dr(4) + 3u, DL_PEDAL, LV_SOFT, 0);   /* a breath of the pedal hat */
    dg_set(DG_HATS, 16, (int32_t)dg_level(DG_HATS));
}
static void dice_perc(dstep_t *d, uint32_t st, uint32_t dens, uint32_t wild)
{
    static const uint8_t L1[DS_COUNT] = {DL_RIM, DL_RIM, DL_CONGA, DL_RIM, DL_SNARE2, DL_CONGA};
    static const uint8_t L2[DS_COUNT] = {DL_BELL, DL_BELL, DL_TOMH, DL_TOML, DL_TOML, DL_BELL};
    static const uint8_t ODD[3] = {12, 13, 15};
    uint32_t len = 16, i, k, rot, clap;
    if ((st == DS_HYPNOTIC || st == DS_DUB || st == DS_ROLLING) && dchance((int32_t)wild))
        len = ODD[dr(3)];
    clap = len == 16u && (st == DS_ACID || st == DS_HARDGROOVE || st == DS_INDUSTRIAL || st == DS_ROLLING ||
                          (st == DS_HYPNOTIC && dens > 80u));
    if (clap) {
        dice_lane(d, 4, DL_CLAP, LV_NORM, 0);
        dice_lane(d, 12, DL_CLAP, LV_NORM, 0);
    }
    k = 3u + dens * (len / 2u - 2u) / 128u;            /* 3 .. len / 2 hits */
    if (st == DS_DUB)
        k = 2u + dens / 64u;
    rot = dr(len);
    for (i = 0; i < len; i++)
        if (euclid(i, k, len, rot) && !(clap && (i == 4u || i == 12u)))
            dice_lane(d, i, L1[st], dchance(40) ? LV_SOFT : LV_NORM,
                      st == DS_INDUSTRIAL && dchance((int32_t)wild / 2) ? 1u + dr(3) : 0u);
    if (dens > 70u) {                                  /* a second voice, sparser, against it */
        k = 2u + dr(3);
        rot = dr(len);
        for (i = 0; i < len; i++)
            if (euclid(i, k, len, rot) && !euclid(i, 3u + dens * (len / 2u - 2u) / 128u, len, 0))
                dice_lane(d, i, L2[st], LV_SOFT, 0);
    }
    if (st == DS_INDUSTRIAL && dchance((int32_t)wild / 2))
        dice_lane(d, 0, DL_CRASH, LV_SOFT, 0);
    dg_set(DG_PERC, (int32_t)len, (int32_t)dg_level(DG_PERC));
}

/* ------------------------------------------------------- synth lines --- */
static uint32_t dice_scale(const track_t *t, uint32_t st, uint8_t *deg)   /* the scale's semitones, how many */
{
    uint32_t m = scale_mask(t), n = 0, i;
    if (m == 0xFFFu)                                   /* chromatic: minor (HYPNOTIC: phrygian) */
        m = st == DS_HYPNOTIC ? 0x5ABu : 0x5ADu;
    for (i = 0; i < 12u; i++)
        if ((m >> i) & 1u)
            deg[n++] = (uint8_t)i;
    return n ? n : 1u;
}
static void dice_put(step_t *s, uint32_t n, const uint8_t *notes, uint32_t time, uint32_t flags)
{
    uint32_t k;
    memset(s, 0, sizeof *s);
    s->n = (uint8_t)n;
    for (k = 0; k < n && k < 4u; k++)
        s->note[k] = notes[k];
    s->time = (uint8_t)time;
    s->flags = (uint8_t)flags;
    s->vel = n ? 100 : 0;
}
static void dice_acid(track_t *t, uint32_t st, uint32_t dens, uint32_t wild)
{
    uint8_t deg[12], nt;
    uint32_t nd = dice_scale(t, st, deg), i, root = 48u + (uint32_t)t->p[P_ROOT] % 12u, prev_on = 0;
    int32_t ton = st == DS_HYPNOTIC ? 80 : 60;         /* the tonic's share (of 127): hypnotic = it all along */
    for (i = 0; i < 16u; i++) {
        step_t *s = &t->step[i];
        int on = i == 0u || dchance(clamp(70 + (int32_t)dens / 3 + (i % 4u == 0u ? 30 : 0), 0, 127));
        if (!on) {
            if (prev_on && dchance(22))
                s->time = ST_TIE;                      /* a held note */
            else
                prev_on = 0;
            continue;
        }
        nt = (uint8_t)(root + (i == 0u || nd < 2u || dchance(ton) ? 0u : deg[1u + dr(nd - 1u)]));
        if (i && dchance((int32_t)wild / 3))
            nt = (uint8_t)(nt + 12u);                  /* an octave jump */
        else if (i && dchance((int32_t)wild / 8))
            nt = (uint8_t)(nt - 12u);
        dice_put(s, 1, &nt, ST_NOTE, (dchance(20 + (int32_t)wild / 3) ? SF_ACCENT : 0u) |
                                     (dchance(8 + (int32_t)wild / 3) ? SF_SLIDE : 0u));
        prev_on = 1;
    }
    for (;;) {                                         /* the tonic the most played note: else one of the most played */
        uint32_t cnt[12] = {0}, best = 0, k;           /* other one becomes the tonic, until it is */
        for (i = 0; i < 16u; i++)
            if (t->step[i].time == ST_NOTE)
                cnt[(t->step[i].note[0] + 12u - root % 12u) % 12u]++;
        for (k = 1; k < 12u; k++)
            best = cnt[k] > cnt[best] ? k : best;
        if (!best || cnt[best] <= cnt[0])
            break;
        for (i = 15; i > 0; i--)
            if (t->step[i].time == ST_NOTE && (t->step[i].note[0] + 12u - root % 12u) % 12u == best) {
                t->step[i].note[0] = (uint8_t)(t->step[i].note[0] - best);   /* (down to the tonic below it) */
                break;
            }
    }
}
static void dice_bass(track_t *t, uint32_t st, uint32_t dens, uint32_t wild)
{
    uint8_t deg[12], nt;
    uint32_t nd = dice_scale(t, st, deg), i, root = 48u + (uint32_t)t->p[P_ROOT] % 12u;
    for (i = 0; i < 16u; i++) {
        int on = i % 4u == 2u || (dens > 64u && i % 4u == 3u && dchance((int32_t)dens - 40)) ||
                 (st == DS_ROLLING && i % 4u == 1u && dchance((int32_t)dens));
        if (!on)
            continue;
        nt = (uint8_t)root;
        if (dchance((int32_t)wild / 3))
            nt = (uint8_t)(root + (nd > 4u ? deg[4] : deg[nd - 1u]));   /* the fifth (or the nearest) */
        else if (dchance((int32_t)wild / 4))
            nt = (uint8_t)(root + 12u);
        dice_put(&t->step[i], 1, &nt, ST_NOTE, 0);
    }
}
static void dice_stabs(track_t *t, uint32_t st, uint32_t dens, uint32_t wild)
{
    uint8_t deg[12], ch[3];
    uint32_t nd = dice_scale(t, st, deg), i, root = 60u + (uint32_t)t->p[P_ROOT] % 12u, k, rot, d0 = 0, j;
    k = st == DS_DUB ? 2u : 3u + dens / 50u;
    rot = dr(16);
    for (i = 0; i < 16u; i++) {
        if (!euclid(i, k, 16, rot))
            continue;
        if (i && dchance((int32_t)wild / 2))
            d0 = dr(nd);                               /* WILD: the chord moves */
        for (j = 0; j < 3u; j++) {                     /* a triad of the scale on degree d0 */
            uint32_t g = d0 + j * 2u;
            ch[j] = (uint8_t)(root + deg[g % nd] + 12u * (g / nd));
        }
        dice_put(&t->step[i], 3, ch, ST_NOTE, 0);
    }
}
static void dice_drone(track_t *t, uint32_t st, uint32_t wild)
{
    uint8_t deg[12], ch[2];
    uint32_t nd = dice_scale(t, st, deg), i, root = 60u + (uint32_t)t->p[P_ROOT] % 12u;
    ch[0] = (uint8_t)root;
    ch[1] = (uint8_t)(root + (nd > 4u ? deg[4] : 7u));
    dice_put(&t->step[0], 2, ch, ST_NOTE, 0);
    for (i = 1; i < 16u; i++)
        t->step[i].time = ST_TIE;
    if (nd > 1u && dchance((int32_t)wild)) {           /* WILD: the second half on another degree */
        ch[0] = (uint8_t)(root + deg[1u + dr(nd - 1u)]);
        dice_put(&t->step[8], 2, ch, ST_NOTE, 0);
    }
}

/* ------------------------------------------------------------ a roll --- */
static void dice_apply(const dice_roll_t *r)
{
    uint32_t st = r->style % DS_COUNT, dens = r->dens, wild = r->wild, g, i;
    dice_rng = (uint32_t)r->seed * 2654435761u ^ 0x9E3779B9u ^ (uint32_t)r->target * 0x85EBCA6Bu;
    if (!dice_rng)
        dice_rng = 1;
    song.g[G_DSTYLE] = (int16_t)st, song.g[G_DDENS] = (int16_t)dens, song.g[G_DWILD] = (int16_t)wild;
    if (r->target < DT_T1) {                           /* the beat, or one of its groups */
        track_t *t = TDRUM;
        uint32_t lanes = 0;
        undo_mark(t, (undo_sess += 4u) | 3u);
        for (g = 0; g < DG_COUNT; g++)
            if (r->target == DT_BEAT || r->target == DT_KICK + g)
                lanes |= dg_lanes(g);
        fm1_irq_off();
        for (i = 0; i < NSTEP; i++)
            for (g = 0; g < DRUM_LANES; g++)
                if ((lanes >> g) & 1u)
                    dstep_clr(&t->dstep[i], g);
        if (r->target == DT_BEAT || r->target == DT_KICK)
            dice_kick(t->dstep, st, wild);
        if (r->target == DT_BEAT || r->target == DT_RUMBLE)
            dice_rumble(t->dstep, st, dens, wild);
        if (r->target == DT_BEAT || r->target == DT_HATS)
            dice_hats(t->dstep, st, dens, wild);
        if (r->target == DT_BEAT || r->target == DT_PERC)
            dice_perc(t->dstep, st, dens, wild);
        if (r->target == DT_BEAT)
            t->p[P_SLEN] = 16;
        t->seq_active = 1;
        fm1_irq_on();
        return;
    }
    {
        track_t *t = &trk[(r->target - DT_T1) % NPART];
        uint32_t p = (uint32_t)(t - trk);
        undo_mark(t, (undo_sess += 4u) | 3u);
        fm1_irq_off();
        steps_clear(t);
        memset(t->micro, 0, sizeof t->micro);
        if (ENGINES[t->eng_req % NENGINES] == &ENG_ACID)
            dice_acid(t, st, dens, wild);
        else if (p == 1u)
            dice_bass(t, st, dens, wild);
        else if (p == 2u)
            dice_stabs(t, st, dens, wild);
        else if (p == 3u)
            dice_drone(t, st, wild);
        else
            dice_acid(t, st, dens, wild);
        t->p[P_SLEN] = 16;
        t->seq_active = 1;
        fm1_irq_on();
    }
}
/* a new roll of `target` with the DICE settings: kept after the one in force (a newer one past it is dropped) */
static void dice_roll(uint32_t target)
{
    dice_roll_t r;
    dice_salt = dice_salt * 1664525u + 1013904223u + fm1_ms;
    r.seed = (uint16_t)(dice_salt >> 16);
    r.target = (uint8_t)(target % DT_COUNT);
    r.style = (uint8_t)(song.g[G_DSTYLE] % DS_COUNT);
    r.dens = (uint8_t)song.g[G_DDENS];
    r.wild = (uint8_t)song.g[G_DWILD];
    if (dice_n && dice_at + 1u < DH_N)
        dice_at++;
    else if (dice_n) {                                 /* full: the oldest goes */
        uint32_t i;
        for (i = 0; i + 1u < DH_N; i++)
            dice_hist[i] = dice_hist[i + 1u];
    }
    dice_hist[dice_at] = r;
    dice_n = (uint8_t)(dice_at + 1u);
    dice_apply(&r);
}
/* OCT- / OCT+ in the DICE layer: the roll before / after (after the newest: a new one of its target) */
static void dice_step(int32_t d)
{
    if (!dice_n)
        return;
    if (d < 0 && dice_at > 0u)
        dice_apply(&dice_hist[--dice_at]);
    else if (d > 0 && dice_at + 1u < dice_n)
        dice_apply(&dice_hist[++dice_at]);
    else if (d > 0)
        dice_roll(dice_hist[dice_at].target);
}
/* MUTATE (a black key in the DICE layer): the target's pattern moved a little, as much as WILD says (at least
 * 1/8): drum hits slide a 16th or change level, a hit comes or goes (the kick's four never: only its pickup); a
 * line's notes step a degree or two along the scale, accents and slides flip, a rest becomes the tonic. Not a new
 * roll: one undo, the history stays */
static void dice_mutate(uint32_t target)
{
    int32_t amt = song.g[G_DWILD] < 16 ? 16 : song.g[G_DWILD];
    uint32_t i, g, l;
    dice_salt = dice_salt * 1664525u + 1013904223u + fm1_ms;
    dice_rng = dice_salt | 1u;
    target %= DT_COUNT;
    if (target < DT_T1) {
        track_t *t = TDRUM;
        undo_mark(t, (undo_sess += 4u) | 3u);
        fm1_irq_off();
        for (g = 0; g < DG_COUNT; g++) {
            uint32_t len = dg_len(g) ? dg_len(g) : trk_len(t);
            if (target != DT_BEAT && target != DT_KICK + g)
                continue;
            for (l = 0; l < DRUM_LANES; l++) {
                if (LANE_GROUP[l] != g)
                    continue;
                for (i = 0; i < len; i++) {
                    dstep_t *d = &t->dstep[i % NSTEP], *n = &t->dstep[(i + 1u) % len % NSTEP];
                    if (l == DL_KICK && i % 4u == 0u)
                        continue;                      /* (four to the floor) */
                    if (!dchance(amt / 2))
                        continue;
                    if (dstep_has(d, l)) {
                        if (!dstep_has(n, l) && dchance(64)) {   /* a 16th later */
                            dstep_set(n, l, dstep_lvl(d, l), dstep_rat(d, l));
                            dstep_clr(d, l);
                            i++;
                        } else {
                            dstep_set(d, l, dstep_lvl(d, l) == LV_NORM ? LV_SOFT : LV_NORM, dstep_rat(d, l));
                        }
                    } else if (l != DL_KICK && dchance(amt / 4)) {
                        dstep_set(d, l, LV_GHOST, 0);   /* a ghost comes */
                    }
                }
            }
        }
        fm1_irq_on();
        return;
    }
    {
        track_t *t = &trk[(target - DT_T1) % NPART];
        uint32_t root = (uint32_t)t->p[P_ROOT] % 12u, len = trk_len(t);
        undo_mark(t, (undo_sess += 4u) | 3u);
        fm1_irq_off();
        for (i = 1; i < len; i++) {
            step_t *s = &t->step[i % NSTEP];
            if (s->time == ST_NOTE && s->n == 1u) {
                if (dchance(amt / 2))
                    s->note[0] = (uint8_t)acc_note(t, s->note[0], dchance(64) ? 1 + (int32_t)dr(2) : -1 - (int32_t)dr(2));
                if (dchance(amt / 4))
                    s->flags ^= SF_ACCENT;
                if (dchance(amt / 5))
                    s->flags ^= SF_SLIDE;
            } else if (s->time == ST_REST && t->step[0].time == ST_NOTE && dchance(amt / 6)) {
                uint8_t nt = (uint8_t)(t->step[0].note[0] - (t->step[0].note[0] + 12u - root) % 12u);   /* the tonic */
                dice_put(s, 1, &nt, ST_NOTE, 0);
            }
        }
        t->seq_active = 1;
        fm1_irq_on();
    }
}

/* the target a roll of the selected track means: the drum track the whole beat, a synth track its line */
static uint32_t dice_target_sel(void) { return song.sel >= NPART ? (uint32_t)DT_BEAT : DT_T1 + song.sel; }
