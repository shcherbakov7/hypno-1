/* SPDX-License-Identifier: GPL-3.0-only */
/* HYPNO-1 stage 5: the sequencer's own screens, as the mockups (the Design artifact: GRID, ROLL), in the SEQ family
 * before STEP: SEQ tapped opens GRID, again ROLL, then STEP, PATTERN, SONG (ui.c open_family).
 *   GRID  MAIN's eight lanes (the drum groups, the synth tracks) as 16 cells each, every one telling its step:
 *         lit by level (a group: its loudest hit), a dark corner an accent (or a hard hit), split by ratchets, hollow
 *         a chance under 100 %, a dark dot a condition (fill, A:B ..); each lane its playhead (polymeter), past its
 *         length nothing. SELECT the page of 16, ALGORITHM the lane; KNOB 1 its LENGTH, 2 RATE (DIV), 3 SWING,
 *         4 ROTATE (a drum group: its own lanes only)
 *   ROLL  the selected synth track as a piano roll: 12 notes of its scale (the root's rows banded), a note a bar as
 *         long as it holds (its TIEs), accented thicker and brighter, a slide a line into the next, ratchets split,
 *         a chance hollow, a condition a dot; the playhead, the cursor (keys write there: the STEP page's entry),
 *         the LOOP WINDOW's columns, past LEN dark. Under it the lock parameter's values (ui.lock_par: the sound
 *         parameter last touched). SELECT the cursor, PRESETS the rows; KNOB 1 the cursor step's LENGTH (TIEs),
 *         2 ACCENT, 3 SLIDE, 4 RATCHET */
static void pattern_rotate(track_t *t, int32_t d);       /* (ui_layers.c) */
static uint32_t steps_hash(const track_t *t);            /* (ui_draw.c) */
static uint8_t grid_page;                                /* GRID: the 16 steps shown */
static uint8_t roll_low = 255;                           /* ROLL: the lowest row's note (255: around the track's notes) */
static int on_seq_screen(void) { return !ui.home && (cur_page()->scope == SC_GRID || cur_page()->scope == SC_ROLL); }

static uint16_t sq_mix(uint16_t a, uint16_t b, uint32_t t256)   /* a -> b, t256 / 256 of the way */
{
    int32_t ar = a >> 11, ag = (a >> 5) & 63, ab = a & 31, br = b >> 11, bg = (b >> 5) & 63, bb = b & 31;
    ar += (br - ar) * (int32_t)t256 / 256, ag += (bg - ag) * (int32_t)t256 / 256, ab += (bb - ab) * (int32_t)t256 / 256;
    return (uint16_t)(ar << 11 | ag << 5 | ab);
}
/* a lit cell: its colour at a level (ghost .. hard), ratchet segments, an accent corner, hollow (a chance), a dot (a
 * condition) */
static void cell_draw(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c, uint32_t rat, int acc, int prob, int cond)
{
    uint32_t n = rat + 1u, k;
    int32_t sw = n > 1u ? (w - (int32_t)(n - 1u)) / (int32_t)n : w;
    for (k = 0; k < n; k++) {
        int32_t sx = x + (int32_t)k * (sw + 1);
        if (prob) {                                       /* hollow: the outline in it, its inside half lit */
            cv_rect(sx, y, sw, h, c);
            cv_rect(sx + 1, y + 1, sw - 2, h - 2, sq_mix(C_BLACK, c, 80));
        } else {
            cv_rect(sx, y, sw, h, c);
        }
    }
    if (acc)
        cv_rect(x + w - 3, y, 3, 3, C_BLACK);
    if (cond)
        cv_rect(x + 1, y + h - 3, 2, 2, C_BLACK);
}

/* ---------------------------------------------------------------- GRID --- */
#define GRID_X 28                                        /* the first cell; 16 at 13 px: to 236 */
#define GRID_PITCH 13
#define GRID_ROW 17
static const char *const GRID_TAG[MAIN_LANES] = {"KCK", "RMB", "HAT", "PRC", "ACD", "BAS", "MEL", "ATM"};
/* step p of lane l: on, its level (LV_*), ratchet, chance under 100 %, a condition */
static int grid_cell(uint32_t l, uint32_t p, uint32_t *lvl, uint32_t *rat, int *prob, int *cond)
{
    uint32_t i;
    *lvl = LV_NORM, *rat = 0, *prob = 0, *cond = 0;
    if (l < DG_COUNT) {
        const dstep_t *d = &TDRUM->dstep[p % NSTEP];
        uint32_t m = dstep_mask(d) & dg_lanes(l), best = 0, on = 0;
        for (i = 0; i < DRUM_LANES; i++) {
            uint32_t v;
            if (!((m >> i) & 1u))
                continue;
            on = 1;
            if (lvl_rank(dstep_lvl(d, i)) >= best)
                best = lvl_rank(dstep_lvl(d, i)), *lvl = dstep_lvl(d, i);
            if (dstep_rat(d, i) > *rat)
                *rat = dstep_rat(d, i);
            v = cond_get(TDRUM, p, i);
            *prob |= (v & 127u) < 100u;
            *cond |= (v >> 8) != 0u;
        }
        *cond |= step_fill(TDRUM, p) != FC_NORM && step_fill(TDRUM, p) != 3u;
        return (int)on;
    }
    {
        const track_t *t = &trk[l - DG_COUNT];
        const step_t *s = &t->step[p % NSTEP];
        uint32_t v = cond_get(t, p, 0);
        if (!step_on(s))
            return s->time == ST_TIE && p ? 2 : 0;      /* (2: a note held on) */
        *lvl = (s->flags & SF_ACCENT) ? LV_HARD : s->lvl & 3u;
        *rat = s->rat & 3u;
        *prob = (v & 127u) < 100u;
        *cond = (v >> 8) != 0u || (step_fill(t, p) != FC_NORM && step_fill(t, p) != 3u);
        return 1;
    }
}
static void grid_draw(void)
{
    static uint32_t head, rows[MAIN_LANES], ruler, footer;
    uint32_t l, j, cur = main_lane(), page = grid_page;
    char title[16];
    str_cpy(title, "grid ", sizeof title);
    fmt_int(title + 5, (int32_t)page * 16 + 1);
    str_cpy(title + str_len(title), "-", 2);
    fmt_int(title + str_len(title), (int32_t)page * 16 + 16);
    te_header(title, TE_TRK[0], &head);
    if (ui.force || ruler != page + 1u) {                 /* the beats over the cells */
        ruler = page + 1u;
        cv_begin(240, 4, C_BLACK);
        for (j = 0; j < 16u; j++)
            cv_rect(GRID_X + (int32_t)j * GRID_PITCH + 4, 1, 3, 2, j % 4u ? TE_G2 : TE_G4);
        cv_blit(0, 40);
    }
    for (l = 0; l < MAIN_LANES; l++) {
        uint32_t tk = lane_track(l), len = lane_len_of(l), pos = lane_pos(l), sel = l == cur, muted = lane_muted(l), h;
        uint16_t col = l < DG_COUNT ? TE_GRP[l] : TE_TRK[tk];
        h = studio_hash(sel + muted * 7u + len * 997u + pos * 7919u + page * 13u, GRID_TAG[l]);
        for (j = 0; j < 16u; j++) {
            uint32_t lv, rt;
            int pr, cd, on = grid_cell(l, page * 16u + j, &lv, &rt, &pr, &cd);
            h = h * 31u + (uint32_t)on * 2u + lv * 5u + rt * 7u + (uint32_t)pr * 11u + (uint32_t)cd * 13u;
        }
        if (!ui.force && h == rows[l])
            continue;
        rows[l] = h;
        cv_begin(240, GRID_ROW, C_BLACK);
        if (sel) {
            cv_rect(0, 0, 240, GRID_ROW, TE_G1);
            cv_rect(0, 1, 26, GRID_ROW - 2, muted ? TE_G2 : col);
            cv_text(1, 0, &FONT_S, GRID_TAG[l], C_BLACK);
        } else {
            cv_text(1, 0, &FONT_S, GRID_TAG[l], muted ? TE_G2 : col);
        }
        for (j = 0; j < 16u; j++) {
            uint32_t p = page * 16u + j, lv, rt;
            int32_t x = GRID_X + (int32_t)j * GRID_PITCH;
            int pr, cd, on;
            uint16_t c;
            if (p >= len) {                               /* past its length: a dot */
                cv_rect(x + 4, 7, 2, 2, TE_G2);
                continue;
            }
            if (p == pos)                                 /* the playhead: a white frame */
                cv_rect(x - 1, 1, GRID_PITCH - 1, GRID_ROW - 2, C_WHITE);
            on = grid_cell(l, p, &lv, &rt, &pr, &cd);
            if (!on) {
                cv_rect(x, 2, GRID_PITCH - 3, GRID_ROW - 4, p == pos ? TE_G2 : TE_G1);
                continue;
            }
            c = lv == LV_HARD ? col : lv == LV_NORM ? sq_mix(C_BLACK, col, 200) : lv == LV_SOFT ? sq_mix(C_BLACK, col, 140)
                                                                                                  : sq_mix(C_BLACK, col, 90);
            if (muted)
                c = sq_mix(C_BLACK, c, 100);
            if (on == 2) {                                /* a synth note held on (TIE): a bar at mid height */
                cv_rect(x - 1, 6, GRID_PITCH - 1, 5, sq_mix(C_BLACK, col, 120));
                continue;
            }
            cell_draw(x, 2, GRID_PITCH - 3, GRID_ROW - 4, c, rt, lv == LV_HARD, pr, cd);
        }
        cv_blit(0, 44 + l * GRID_ROW);
    }
    {   /* KNOB 1 LENGTH, 2 RATE, 3 SWING of the lane / its track, 4 ROTATE */
        track_t *t = &trk[lane_track(cur)];
        static char v[4][8];
        static const char *const lab[4] = {"length", "rate", "swing", "rotate"};
        const char *val[4] = {v[0], v[1], v[2], v[3]};
        int32_t ratio[4] = {-1, -1, -1, -1};
        uint32_t len = lane_len_of(cur);
        fmt_int(v[0], (int32_t)len);
        str_cpy(v[1], N_SDIV[t->p[P_SDIV] % NDIV_STEP], 8);
        fmt_int(v[2], t->p[P_SSWING]);
        str_cpy(v[3], "<  >", 8);
        ratio[0] = ((int32_t)len - 1) * 1000 / (NSTEP - 1);
        ratio[1] = t->p[P_SDIV] * 1000 / (NDIV_STEP - 1);
        ratio[2] = t->p[P_SSWING] * 10;
        te_dials(184, lab, val, ratio, cur + 11u, &footer);
    }
}
static void group_rotate(uint32_t g, int32_t d)          /* a drum group's lanes one step later / earlier, in its length */
{
    uint32_t len = dg_len(g) ? dg_len(g) : trk_len(TDRUM), m = dg_lanes(g), l, i;
    undo_mark(TDRUM, (undo_sess += 4u) | 3u);
    fm1_irq_off();
    for (l = 0; l < DRUM_LANES; l++) {
        dstep_t keep;
        if (!((m >> l) & 1u))
            continue;
        if (d > 0) {
            keep = TDRUM->dstep[(len - 1u) % NSTEP];
            for (i = len - 1u; i > 0; i--) {
                if (dstep_has(&TDRUM->dstep[i - 1u], l))
                    dstep_set(&TDRUM->dstep[i], l, dstep_lvl(&TDRUM->dstep[i - 1u], l), dstep_rat(&TDRUM->dstep[i - 1u], l));
                else
                    dstep_clr(&TDRUM->dstep[i], l);
            }
            if (dstep_has(&keep, l))
                dstep_set(&TDRUM->dstep[0], l, dstep_lvl(&keep, l), dstep_rat(&keep, l));
            else
                dstep_clr(&TDRUM->dstep[0], l);
        } else {
            keep = TDRUM->dstep[0];
            for (i = 0; i + 1u < len; i++) {
                if (dstep_has(&TDRUM->dstep[i + 1u], l))
                    dstep_set(&TDRUM->dstep[i], l, dstep_lvl(&TDRUM->dstep[i + 1u], l), dstep_rat(&TDRUM->dstep[i + 1u], l));
                else
                    dstep_clr(&TDRUM->dstep[i], l);
            }
            if (dstep_has(&keep, l))
                dstep_set(&TDRUM->dstep[(len - 1u) % NSTEP], l, dstep_lvl(&keep, l), dstep_rat(&keep, l));
            else
                dstep_clr(&TDRUM->dstep[(len - 1u) % NSTEP], l);
        }
    }
    fm1_irq_on();
}
static void grid_knob(uint32_t k, int32_t s)
{
    uint32_t cur = main_lane();
    track_t *t = &trk[lane_track(cur)];
    if (k == 0u) {
        if (cur < DG_COUNT)
            dg_set(cur, clamp((int32_t)lane_len_of(cur) + s, 1, NSTEP), (int32_t)dg_level(cur));
        else
            t->p[P_SLEN] = (int16_t)clamp(t->p[P_SLEN] + s, 1, NSTEP);
    } else if (k == 1u) {
        t->p[P_SDIV] = (int16_t)clamp(t->p[P_SDIV] + s, 0, (int32_t)NDIV_STEP - 1);
    } else if (k == 2u) {
        t->p[P_SSWING] = (int16_t)clamp(t->p[P_SSWING] + accel(EN_K3, s, 100), 0, 100);
    } else if (cur < DG_COUNT) {
        group_rotate(cur, s);
    } else {
        pattern_rotate(t, s);
    }
}

/* ---------------------------------------------------------------- ROLL --- */
#define ROLL_ROWS 12u
#define ROLL_RH 9                                        /* 12 x 9 = 108 px, y 44 .. 152 */
#define ROLL_X 16                                        /* the first column; 16 at 14 px: to 240 */
#define ROLL_CW 14
static uint32_t roll_scale(const track_t *t, uint8_t *row)   /* the notes of the 12 rows, low to high */
{
    uint32_t m = scale_mask(t), root = (uint32_t)t->p[P_ROOT] % 12u, n = roll_low, k = 0, guard = 128;
    if (n == 255u) {                                     /* around the pattern's notes: its lowest near the bottom */
        uint32_t lo = 127, i, j;
        for (i = 0; i < trk_len(t); i++)
            for (j = 0; j < t->step[i].n && t->step[i].time == ST_NOTE; j++)
                lo = t->step[i].note[j] < lo ? t->step[i].note[j] : lo;
        n = lo == 127u ? 48u : lo;
        n = n > 2u ? n - 2u : 0u;
    }
    while (k < ROLL_ROWS && n < 128u && guard--) {
        if ((m >> ((n + 12u - root) % 12u)) & 1u)
            row[k++] = (uint8_t)n;
        n++;
    }
    while (k < ROLL_ROWS)
        row[k] = (uint8_t)(k ? row[k - 1u] + 1u : 0u), k++;
    return root;
}
static int32_t roll_row_of(const uint8_t *row, uint32_t note)   /* -1 below, ROLL_ROWS above, the nearest row */
{
    uint32_t r;
    if (note < row[0])
        return -1;
    if (note > row[ROLL_ROWS - 1u])
        return ROLL_ROWS;
    for (r = 0; r + 1u < ROLL_ROWS && row[r + 1u] <= note; r++)
        ;
    return (int32_t)r;
}
static uint32_t roll_hold(const track_t *t, uint32_t i)   /* steps note i holds: itself and its TIEs */
{
    uint32_t n = 1, len = trk_len(t);
    while (i + n < len && n < 16u && t->step[(i + n) % NSTEP].time == ST_TIE)
        n++;
    return n;
}
static void roll_draw(void)
{
    static uint32_t head, area, lane, footer;
    track_t *t = TSEL;
    uint8_t row[ROLL_ROWS];
    uint32_t page = ui.cursor / 16u, len = trk_len(t), pos = song.playing && t->seq_abs != SEQ_NONE ? t->seq_idx : 0xFFu;
    uint32_t root = roll_scale(t, row), i, j, h;
    uint16_t col = TE_TRK[song.sel % NTRK];
    char title[16];
    str_cpy(title, "roll ", sizeof title);
    str_cpy(title + 5, N_NOTE[root], 4);
    te_lower(title, title, sizeof title);
    te_header(title, col, &head);
    h = steps_hash(t) * 31u + ui.cursor * 7u + pos * 13u + row[0] * 17u + loop_win * 19u + len * 23u + t->acc_k * 29u;
    for (i = 0; i < NLOCK; i++)
        h = h * 3u + t->lock[i].step * 5u + (uint32_t)t->lock[i].val;
    if (ui.force || h != area) {
        area = h;
        cv_begin(240, ROLL_ROWS * ROLL_RH + 4u, C_BLACK);
        for (j = 0; j < ROLL_ROWS; j++) {                /* the rows, the root's banded */
            int32_t y = (int32_t)(ROLL_ROWS - 1u - j) * ROLL_RH + 2;
            if (row[j] % 12u == root) {
                cv_rect(ROLL_X, y, 240 - ROLL_X, ROLL_RH - 1, TE_G1);
                cv_rect(0, y + 2, 10, ROLL_RH - 5, TE_G3);
            }
            cv_rect(ROLL_X, y + ROLL_RH - 1, 240 - ROLL_X, 1, sq_mix(C_BLACK, TE_G1, 160));
        }
        for (i = 0; i <= 16u; i += 4u)                   /* the beats */
            cv_rect(ROLL_X + (int32_t)i * ROLL_CW - 1, 2, 1, ROLL_ROWS * ROLL_RH, TE_G2);
        if (loop_win && pos != 0xFFu && pos / 16u == page) {   /* the LOOP WINDOW repeating */
            uint32_t w0 = pos - pos % loop_win;
            cv_rect(ROLL_X + (int32_t)(w0 % 16u) * ROLL_CW, 0, (int32_t)loop_win * ROLL_CW, 2, col);
        }
        if (pos != 0xFFu && pos / 16u == page)           /* the playhead */
            cv_rect(ROLL_X + (int32_t)(pos % 16u) * ROLL_CW, 2, 2, ROLL_ROWS * ROLL_RH, C_WHITE);
        for (i = 0; i < 16u; i++) {                      /* the notes */
            uint32_t p = page * 16u + i, k;
            const step_t *s = &t->step[p % NSTEP];
            uint32_t v;
            if (p >= len)
                break;
            if (!step_on(s))
                continue;
            v = cond_get(t, p, 0);
            for (k = 0; k < s->n && k < 4u; k++) {
                int32_t r = roll_row_of(row, s->note[k]), x = ROLL_X + (int32_t)i * ROLL_CW + 1, y;
                int acc = (s->flags & SF_ACCENT) != 0;
                uint32_t hold = roll_hold(t, p), w;
                uint16_t c = acc ? col : sq_mix(C_BLACK, col, 170);
                if (r < 0 || r >= (int32_t)ROLL_ROWS) {   /* off the rows: a tick at the edge */
                    cv_rect(x + 3, r < 0 ? (int32_t)(ROLL_ROWS * ROLL_RH) : 0, 6, 2, col);
                    continue;
                }
                y = (int32_t)(ROLL_ROWS - 1u - (uint32_t)r) * ROLL_RH + 2 + (acc ? 0 : 1);
                w = hold * ROLL_CW - 3u;
                if ((int32_t)w + x > 239)
                    w = (uint32_t)(239 - x);
                cell_draw(x, y, (int32_t)w, acc ? ROLL_RH - 1 : ROLL_RH - 3, c, (s->rat >> (2u * k)) & 3u, 0, (v & 127u) < 100u,
                          (v >> 8) != 0u || (step_fill(t, p) != FC_NORM && step_fill(t, p) != 3u));
                if ((s->flags & SF_SLIDE) && k == 0u && p + hold < len) {   /* a slide: a line into the next note */
                    const step_t *nx = &t->step[(p + hold) % NSTEP];
                    if (step_on(nx)) {
                        int32_t r2 = roll_row_of(row, nx->note[0]);
                        if (r2 >= 0 && r2 < (int32_t)ROLL_ROWS)
                            cv_line(x + (int32_t)w - 2, y + 3, x + (int32_t)hold * ROLL_CW + 1,
                                    (int32_t)(ROLL_ROWS - 1u - (uint32_t)r2) * ROLL_RH + 5, C_WHITE);
                    }
                }
            }
        }
        if (len < page * 16u + 16u) {                    /* past LEN: dark */
            uint32_t from = len > page * 16u ? len - page * 16u : 0u;
            cv_rect(ROLL_X + (int32_t)from * ROLL_CW - 1, 2, 240 - ROLL_X - (int32_t)from * ROLL_CW + 1, ROLL_ROWS * ROLL_RH, TE_G1);
        }
        {   /* the cursor: a frame round its column */
            int32_t x = ROLL_X + (int32_t)(ui.cursor % 16u) * ROLL_CW - 1;
            cv_rect(x, 0, ROLL_CW + 1, 1, C_WHITE);
            cv_rect(x, (int32_t)(ROLL_ROWS * ROLL_RH) + 3, ROLL_CW + 1, 1, C_WHITE);
        }
        cv_blit(0, 42);
    }
    {   /* under it: the lock parameter's value on each step (a lock: lit, else the track's value, dim) */
        uint32_t id = ui.lock_par % P_COUNT, hs = id * 7u + h;
        if (ui.force || hs != lane) {
            const param_desc_t *d = lock_desc(t, id);
            int32_t span = d->max - d->min > 0 ? d->max - d->min : 1;
            char lb[12];
            lane = hs;
            cv_begin(240, 28, C_BLACK);
            te_lower(lb, track_desc(t, id)->label, 8);
            cv_text(240 - (int32_t)text_w(&FONT_S, lb), -4, &FONT_S, lb, TE_G3);   /* (its name, top right) */
            for (i = 0; i < 16u && page * 16u + i < len; i++) {
                int k = lock_find(t, page * 16u + i, id, 0);
                int32_t v = k >= 0 ? t->lock[k].val : t->p[id], bh = (v - d->min) * 14 / span + 1;
                if (!step_on(&t->step[(page * 16u + i) % NSTEP]))
                    continue;
                cv_rect(ROLL_X + (int32_t)i * ROLL_CW + 4, 26 - bh, 6, bh, k >= 0 ? col : TE_G2);
            }
            cv_blit(0, 154);
        }
    }
    {   /* the cursor step: KNOB 1 LENGTH, 2 ACCENT, 3 SLIDE, 4 RATCHET */
        const step_t *s = &t->step[ui.cursor % NSTEP];
        static char v[4][8];
        static const char *const lab[4] = {"length", "accent", "slide", "ratchet"};
        const char *val[4] = {v[0], v[1], v[2], v[3]};
        int32_t ratio[4] = {-1, -1, -1, -1};
        if (step_on(s)) {
            fmt_int(v[0], (int32_t)roll_hold(t, ui.cursor));
            str_cpy(v[1], (s->flags & SF_ACCENT) ? "on" : "off", 8);
            str_cpy(v[2], (s->flags & SF_SLIDE) ? "on" : "off", 8);
            v[3][0] = 'x', v[3][1] = (char)('1' + (s->rat & 3u)), v[3][2] = 0;
            ratio[0] = (int32_t)(roll_hold(t, ui.cursor) - 1u) * 1000 / 15;
        } else {
            str_cpy(v[0], "--", 8), str_cpy(v[1], "--", 8), str_cpy(v[2], "--", 8), str_cpy(v[3], "--", 8);
        }
        te_dials(184, lab, val, ratio, song.sel + 23u, &footer);
    }
}
static void roll_knob(uint32_t k, int32_t s)
{
    track_t *t = TSEL;
    step_t *st = &t->step[ui.cursor % NSTEP];
    uint32_t len = trk_len(t), i;
    if (!step_on(st))
        return;
    undo_mark(t, (undo_sess += 4u) | 3u);
    fm1_irq_off();
    if (k == 0u) {                                       /* LENGTH: TIEs after it (into rests; a note after stops it) */
        uint32_t hold = roll_hold(t, ui.cursor), want = (uint32_t)clamp((int32_t)hold + s, 1, 16);
        for (i = hold; i < want && ui.cursor + i < len; i++) {
            step_t *n = &t->step[(ui.cursor + i) % NSTEP];
            if (step_on(n))
                break;
            memset(n, 0, sizeof *n);
            n->time = ST_TIE;
        }
        for (i = want; i < hold; i++) {
            step_t *n = &t->step[(ui.cursor + i) % NSTEP];
            memset(n, 0, sizeof *n);
            n->time = ST_REST;
        }
    } else if (k == 1u) {
        st->flags = (uint8_t)(s > 0 ? st->flags | SF_ACCENT : st->flags & ~SF_ACCENT);
    } else if (k == 2u) {
        st->flags = (uint8_t)(s > 0 ? st->flags | SF_SLIDE : st->flags & ~SF_SLIDE);
    } else {
        uint32_t r = (uint32_t)clamp((int32_t)(st->rat & 3u) + s, 0, 3), j;
        for (j = 0; j < 4u; j++)
            st->rat = (uint8_t)((st->rat & ~(3u << (2u * j))) | r << (2u * j));
    }
    fm1_irq_on();
}

/* the knobs of both (the buttons, PLAY, OCT, HOME: as on every page, ui_input.c); the keys write the ROLL's cursor
 * step (seq_entry, ui_input.c) */
static void seq_screen_input(void)
{
    uint32_t k, roll = cur_page()->scope == SC_ROLL;
    int32_t s;
    if (roll && is_drum(TSEL)) {                         /* (ROLL is a synth track's: the drums go to GRID) */
        studio_open(SC_GRID);
        return;
    }
    if ((s = panel_enc(EN_SELECT)) != 0) {
        if (roll) {
            cursor_set((int32_t)ui.cursor + s);
        } else {
            grid_page = (uint8_t)clamp((int32_t)grid_page + (s > 0 ? 1 : -1), 0, NSTEP / 16 - 1);
            ui.force = 1;
        }
    }
    if ((s = panel_enc(EN_ALGO)) != 0 && !ft_on) {
        lane_step(s);
        ui.force = 1;
    }
    if ((s = panel_enc(EN_PRESET)) != 0 && roll) {       /* the rows: a scale note a detent */
        uint8_t row[ROLL_ROWS];
        uint32_t m = scale_mask(TSEL), root = (uint32_t)TSEL->p[P_ROOT] % 12u, n;
        roll_scale(TSEL, row);
        n = row[0];
        if (s > 0)
            n = row[1] <= 115u ? row[1] : n;
        else
            while (n > 0u && !((m >> ((--n + 12u - root) % 12u)) & 1u))
                ;
        roll_low = (uint8_t)n;
        ui.force = 1;
    }
    for (k = 0; k < 4u; k++)
        if ((s = panel_enc(EN_K1 + k)) != 0) {
            ui.hot_col = (uint8_t)k;
            ui.hot_t = 40;
            if (roll)
                roll_knob(k, s);
            else
                grid_knob(k, s);
        }
}
static void seq_screen_draw(void)
{
    if (cur_page()->scope == SC_ROLL)
        roll_draw();
    else
        grid_draw();
}
