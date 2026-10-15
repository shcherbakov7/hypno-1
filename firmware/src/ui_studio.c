/* SPDX-License-Identifier: GPL-3.0-only */
/* LIVE screens, 240 x 240, in a teenage-engineering-like style: black, four colours (one per
 * track and per knob: blue, green, yellow, orange), white for what you touch, red for recording,
 * big numbers, lowercase labels, four dials at the bottom that show what KNOB 1..4 do.
 * Screens: TRACKS (the performance view), DRUMS (GRID / KIT, pads that flash on each hit), the
 * LAYERS (a function button held: what the 16 white keys and the knobs do now), HOLD (a hold to
 * confirm: clear, save), REC (armed / free take). Every band is drawn into the canvas only when its
 * signature changed. */
static uint8_t drum_page, drum_lane, drum_cursor;   /* drum_page: 0 GRID, 1 KIT, 2 GROUPS (HYPNO-1) */
static uint8_t drum_grp;                            /* GROUPS: the lane group KNOB 2..4 set (drums.c DG_*) */
#define DRUM_PAGES 3u
static void trk_short_name(uint32_t c, char *b);
static int on_drum_page(void) { return !ui.home && cur_page()->scope == SC_DRUM; }

/* ---------------------------------------------------------------- style --- */
#define TE_G1 RGB(26, 26, 30)            /* tiles */
#define TE_G2 RGB(54, 54, 60)            /* empty steps, dial tracks */
#define TE_G3 RGB(118, 118, 126)         /* labels */
#define TE_G4 RGB(196, 196, 204)         /* secondary text */
#define TE_RED RGB(255, 44, 52)          /* recording, erasing */
static const uint16_t TE_COL[4] = {RGB(40, 124, 255), RGB(30, 204, 112), RGB(255, 198, 24), RGB(255, 98, 26)};
static const uint16_t TE_MID[4] = {RGB(26, 82, 170), RGB(20, 136, 76), RGB(170, 132, 16), RGB(170, 66, 18)};
static const uint16_t TE_DIM[4] = {RGB(14, 40, 86), RGB(10, 66, 38), RGB(86, 66, 8), RGB(86, 32, 8)};
/* HYPNO-1: TE_COL / TE_MID / TE_DIM are the knobs' colours (KNOB 1..4); the tracks have their own (the screen
 * designs): ACID lime, BASS green, MELO teal, ATMOS violet, the drums orange; and the drums' lane groups on MAIN:
 * KICK red, RUMBLE pink, HATS yellow, PERC orange */
static const uint16_t TE_TRK[NTRK] = {RGB(182, 255, 46), RGB(61, 220, 132), RGB(51, 214, 208), RGB(167, 121, 255),
                                      RGB(255, 138, 43)};
static const uint16_t TE_TRK_MID[NTRK] = {RGB(116, 166, 28), RGB(38, 142, 84), RGB(32, 138, 134), RGB(108, 78, 168),
                                          RGB(170, 90, 26)};
static const uint16_t TE_TRK_DIM[NTRK] = {RGB(52, 76, 12), RGB(18, 66, 40), RGB(14, 64, 62), RGB(50, 36, 80),
                                          RGB(80, 42, 12)};
static const uint16_t TE_GRP[DG_COUNT] = {RGB(255, 74, 61), RGB(224, 80, 122), RGB(255, 210, 63), RGB(255, 138, 43)};
static const uint16_t TE_GRP_DIM[DG_COUNT] = {RGB(84, 24, 20), RGB(74, 26, 40), RGB(84, 68, 20), RGB(80, 42, 12)};
#define TE_DRUM TE_TRK[TRK_DRUM]

static void te_disc(int32_t cx, int32_t cy, int32_t r, uint16_t c)     /* filled circle */
{
    int32_t y, x;
    for (y = -r; y <= r; y++)
        for (x = -r; x <= r; x++)
            if (x * x + y * y <= r * r + r)
                cv_pset(cx + x, cy + y, c);
}
/* a dial: a 270-degree ring (lit up to the value), a pointer; ratio 0..1000, -1 = a plain ring */
static void te_dial(int32_t cx, int32_t cy, int32_t r, int32_t ratio, uint16_t c, uint16_t dim)
{
    int32_t i, end = ratio < 0 ? 768 : ratio * 768 / 1000;
    for (i = 0; i <= 768; i += 8) {
        uint32_t a = (uint32_t)(384 + i) & 1023u;
        int32_t co = SINE[(a + 256u) & 1023u], si = SINE[a], k;
        uint16_t col = ratio < 0 || i <= end ? c : dim;
        for (k = r - 2; k <= r; k++)
            cv_pset(cx + ((co * k) >> 15), cy + ((si * k) >> 15), col);
    }
    if (ratio >= 0) {
        uint32_t a = (uint32_t)(384 + end) & 1023u;
        int32_t co = SINE[(a + 256u) & 1023u], si = SINE[a];
        cv_line(cx, cy, cx + ((co * (r - 4)) >> 15), cy + ((si * (r - 4)) >> 15), C_WHITE);
        te_disc(cx, cy, 2, C_WHITE);
    }
}
static void te_play_icon(int32_t x, int32_t y, int playing)
{
    int32_t i;
    if (playing)
        for (i = 0; i < 12; i++)
            cv_rect(x + i, y + i / 2, 1, 14 - i, C_WHITE);   /* a triangle */
    else
        cv_rect(x, y + 1, 12, 12, TE_G3);
}
static uint32_t studio_hash(uint32_t h, const char *p)
{ while (*p) h = h * 31u + (uint8_t)*p++; return h; }
static void te_lower(char *d, const char *s, uint32_t n)
{
    uint32_t i;
    for (i = 0; i + 1u < n && s[i]; i++)
        d[i] = s[i] >= 'A' && s[i] <= 'Z' ? (char)(s[i] + 32) : s[i];
    d[i] = 0;
}
static void te_text_c(int32_t cx, int32_t y, const char *s, uint16_t c)   /* centred on cx */
{
    cv_text(cx - text_w(&FONT_S, s) / 2, y, &FONT_S, s, c);
}

/* the dial strip: KNOB 1..4, label + value under each (a dial with no label: an empty column); a
 * message replaces it */
static void te_dials(int32_t y0, const char *const lab[4], const char *const val[4], const int32_t ratio[4],
                     uint32_t sig, uint32_t *cache)
{
    uint32_t k;
    sig = studio_hash(sig, ui.msg_t ? ui.msg : "");
    for (k = 0; k < 4u; k++) {
        sig = studio_hash(sig * 7u + (uint32_t)ratio[k] + (ui.hot_t && ui.hot_col == k) * 5003u, lab[k]);
        sig = studio_hash(sig, val[k]);
    }
    if (!ui.force && sig == *cache)
        return;
    *cache = sig;
    cv_begin(240, ui.msg_t ? 240u - (uint32_t)y0 : 40u, C_BLACK);
    if (ui.msg_t) {                                     /* a message: a white bar */
        cv_rect(0, 10, 240, 32, C_WHITE);
        cv_text((240 - text_w(&FONT_S, ui.msg)) / 2, 18, &FONT_S, ui.msg, C_BLACK);
        cv_blit(0, (uint32_t)y0);
        return;
    }
    for (k = 0; k < 4u; k++) {
        int32_t cx = 30 + 60 * (int32_t)k;
        if (!lab[k][0])
            continue;
        te_dial(cx, 12, 11, ratio[k], TE_COL[k], TE_DIM[k]);
        te_text_c(cx, 24, lab[k], TE_G3);
    }
    cv_blit(0, (uint32_t)y0);
    cv_begin(240, 16, C_BLACK);                         /* the values: white while turned */
    for (k = 0; k < 4u; k++)
        te_text_c(30 + 60 * (int32_t)k, 0, val[k], ui.hot_t && ui.hot_col == k ? C_WHITE : TE_G4);
    cv_blit(0, (uint32_t)y0 + 40u);
}

static void studio_open(uint32_t scope)
{
    uint32_t i;
    if (scope == SC_SONG && rec_wait)
        rec_wait = 0;                                   /* (an arm does not follow into the song page) */
    for (i = 0; i < NPAGES; i++) if (PAGES[i].scope == scope) {
        ui.page = (uint8_t)i; ui.home = 0; song.seq_mode = 0; ui.force = 1;
        ui.msg_t = 0; ui.entry_open = 0; return;
    }
}
/* LIVE: REC, on any page. One record arm, on the selected track (it follows ALGORITHM). No
 * click (seq.c): playing, recording starts at once; stopped, REC arms and the first note
 * starts the loop (or, in an empty project, a free take that REC closes: seq.c ft_close).
 * REC again stops recording / cancels the arm (playback continues). */
static void rec_toggle(void)
{
    if (ft_owns_press())
        return;
    if (song.playing && arrangement_enabled) {
        ui_message("STOP THE SONG FIRST");
        return;
    }
    if (song.rec || rec_wait) {
        song.rec = 0;
        rec_wait = 0;
        ui_message("REC OFF");
        return;
    }
    arrangement_enabled = 0;
    if (song.playing)
        rec_begin();                    /* playing: record now */
    else
        rec_wait = 1;                   /* stopped: the first note starts it */
}

/* the loop position of track t: "bar.beat" in its pattern (1.1 .. ) */
static void loop_pos(const track_t *t, char *b)
{
    uint32_t len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP), s = t->seq_idx % len, k;
    fmt_int(b, (int32_t)(s / 16u + 1u));
    k = str_len(b);
    b[k] = '.';
    b[k + 1] = (char)('1' + (s % 16u) / 4u);
    b[k + 2] = 0;
}

/* the header of the live screens: BPM, transport, the loop position, the beat lights, REC */
static void te_header(const char *title, uint16_t tc, uint32_t *cache)
{
    char b[12];
    uint32_t beat = clk_beat, k, playing = song.playing;
    uint32_t sig = studio_hash((uint32_t)song.g[G_BPM] * 7u + playing * 3u + (song.rec != 0) * 1999u +
                               (playing ? beat * 131u + TSEL->seq_idx * 7919u : 0u) + (uint32_t)song.g[G_CLOCK] * 77u +
                               arrangement_enabled * 5u + (ui.bpm_t != 0) * 104729u + song.solo * 37u, title);
    sig = sig * 31u + tc;
    if (!ui.force && sig == *cache)
        return;
    *cache = sig;
    cv_begin(240, 40, C_BLACK);
    fmt_int(b, song.g[G_BPM]);
    cv_text(4, 4, &FONT_L, b, ui.bpm_t ? C_WHITE : TE_G4);
    cv_text(4 + text_w(&FONT_L, b) + 4, 20, &FONT_S, "bpm", TE_G3);
    te_play_icon(104, 6, (int)playing);
    if (playing) {                                     /* bar.beat in the selected track's loop */
        loop_pos(TSEL, b);
        cv_text(122, 5, &FONT_S, b, C_WHITE);
    } else {
        cv_text(122, 5, &FONT_S, arrangement_enabled ? "song" : "loop", TE_G3);
    }
    for (k = 0; k < 4u; k++)                           /* the four beats of the bar */
        cv_rect(104 + (int32_t)k * 9, 26, 7, 7, playing && beat % 4u == k ? (k ? C_WHITE : TE_RED) : TE_G2);
    if (song.rec) {
        te_disc(224, 12, 8, TE_RED);
        cv_text(176, 4, &FONT_S, "rec", TE_RED);
        if (song.g[G_CLOCK] != 0)                       /* the click is on */
            cv_text(168, 22, &FONT_S, "click", TE_G3);
    } else {
        cv_text(240 - text_w(&FONT_S, title) - 4, 5, &FONT_S, title, tc);
        if (song.solo)
            cv_text(236 - text_w(&FONT_S, "solo"), 22, &FONT_S, "solo", C_WHITE);
    }
    cv_rect(0, 39, 240, 1, TE_G1);
    cv_blit(0, 0);
}

/* --------------------------------------------------------------- TRACKS --- */
/* swing, MPC style: "62%" */
static void swing_str(char *b, int32_t v)            /* 0 straight .. 100 the most (params.c F_SWING) */
{
    fmt_int(b, clamp(v, 0, 100));
}

/* HYPNO-1 MAIN (where SLOOP had TRACKS): eight lanes, the drums' lane groups KICK RUMBLE HATS PERC (each on its own
 * loop, drums.c) and the synth tracks ACID BASS MELO ATMOS; each its 16 steps in view with its own playhead, its
 * position / length, its level (or MUTE / SOLO), the lane ALGORITHM is on (ui_input.c lane_select) lit */
#define MAIN_LANES (DG_COUNT + NPART)
#define MAIN_ROW 18u                     /* 8 x 18 in the 144 px above the dials */
static const char *const MAIN_NAME[MAIN_LANES] = {"KICK", "RUMBLE", "HATS", "PERC", "ACID", "BASS", "MELO", "ATMOS"};
static uint32_t main_lane(void) { return song.sel == TRK_DRUM ? drum_grp % DG_COUNT : DG_COUNT + song.sel % NPART; }
static uint32_t lane_track(uint32_t l) { return l < DG_COUNT ? TRK_DRUM : l - DG_COUNT; }
static uint32_t lane_len_of(uint32_t l)
{
    return l < DG_COUNT ? (dg_len(l) ? dg_len(l) : trk_len(TDRUM)) : trk_len(&trk[l - DG_COUNT]);
}
static uint32_t lane_pos(uint32_t l)            /* the lane's step now, 0xFF: stopped */
{
    const track_t *t = &trk[lane_track(l)];
    if (!song.playing || t->seq_abs == SEQ_NONE)
        return 0xFFu;
    return l < DG_COUNT ? t->seq_abs % lane_len_of(l) : t->seq_idx % lane_len_of(l);
}
static int lane_on(uint32_t l, uint32_t p)
{
    return l < DG_COUNT ? (dstep_mask(&TDRUM->dstep[p % NSTEP]) & dg_lanes(l)) != 0u : trk_step_on(&trk[l - DG_COUNT], p);
}
static int lane_muted(uint32_t l)
{
    return trk_silent(&trk[lane_track(l)]) || (l < DG_COUNT && !(dg_lanes(l) & ~(uint32_t)drum_mute)) ||
           (l < DG_COUNT ? !dg_level(l) || !song.g[G_DRLVL] : !trk[l - DG_COUNT].p[P_LEVEL]);
}
static uint32_t lane_level(uint32_t l)          /* 0..127 */
{
    return l < DG_COUNT ? dg_level(l) * (uint32_t)song.g[G_DRLVL] / 127u : (uint32_t)trk[l - DG_COUNT].p[P_LEVEL] & 127u;
}
/* ALGORITHM, one step (s: its sign) through MAIN's eight lanes: a drum lane selects the drum track and its group */
static void lane_step(int32_t s)
{
    uint32_t l = (uint32_t)clamp((int32_t)main_lane() + (s > 0 ? 1 : -1), 0, MAIN_LANES - 1);
    if (l < DG_COUNT) {
        if (drum_grp != l)
            ui.force = 1;
        drum_grp = (uint8_t)l;
    }
    track_select(lane_track(l));
}
static void studio_tracks_draw(void)
{
    static uint32_t head, rows[MAIN_LANES], footer;
    uint32_t l, j, cur = main_lane();
    {   /* the title: the load, core 0 / core 1 (one core: core 0's), at most twice a second */
        static char t[16];
        static uint32_t shown;
        if (!t[0] || fm1_ms - shown >= 500u) {
            uint32_t k;
            shown = fm1_ms;
            str_cpy(t, "cpu ", sizeof t);
            fmt_int(t + 4, (int32_t)(song.cpu_q8 * 100u / 256u));
#if FELUCCA_CPU2
            if (fm1_c1_on) {
                k = str_len(t);
                t[k] = '/';
                fmt_int(t + k + 1u, (int32_t)(c1_q8 * 100u / 256u));
            }
#endif
            k = str_len(t);
            t[k] = '%', t[k + 1u] = 0;
        }
        te_header(t, TE_G3, &head);
    }
    for (l = 0; l < MAIN_LANES; l++) {
        uint32_t tk = lane_track(l), len = lane_len_of(l), pos = lane_pos(l), sel = l == cur, muted = lane_muted(l);
        uint32_t rec = (song.rec >> tk) & 1u, solo = (song.solo >> tk) & 1u, level = lane_level(l);
        uint32_t page = pos != 0xFFu ? pos / 16u : 0u, h;
        uint16_t col = l < DG_COUNT ? TE_GRP[l] : TE_TRK[tk], dim = l < DG_COUNT ? TE_GRP_DIM[l] : TE_TRK_DIM[tk];
        char b[8];
        h = studio_hash(sel + muted * 7u + rec * 31u + solo * 131u + len * 997u + pos * 7919u + level * 3u + page * 13u,
                        MAIN_NAME[l]);
        for (j = 0; j < 16u; j++)
            h = h * 3u + (uint32_t)lane_on(l, page * 16u + j);
        if (!ui.force && h == rows[l])
            continue;
        rows[l] = h;
        cv_begin(240, MAIN_ROW, C_BLACK);
        if (sel)
            cv_rect(0, 0, 240, MAIN_ROW, TE_G1);
        cv_rect(2, 3, 4, 12, rec ? TE_RED : muted ? TE_G2 : col);   /* the lane's tag (red: recording) */
        cv_text(10, 1, &FONT_S, MAIN_NAME[l], muted ? TE_G3 : sel ? C_WHITE : TE_G4);
        for (j = 0; j < 16u; j++) {                /* the 16 steps in view: the playhead's page */
            uint32_t p = page * 16u + j, on;
            uint16_t c;
            if (p >= len)
                break;
            on = (uint32_t)lane_on(l, p);
            c = on ? (muted ? TE_G3 : sel ? col : dim) : sel ? TE_G2 : TE_G1;
            if (on && !muted && !sel)
                c = col;
            if (p == pos)
                c = on ? C_WHITE : TE_G3;
            cv_rect(62 + (int32_t)j * 9, 4, 7, 10, c);
        }
        if (solo) {
            cv_rect(207, 2, 32, 14, C_WHITE);
            cv_text(207, 1, &FONT_S, "solo", C_BLACK);
        } else if (muted) {
            cv_text(207, 1, &FONT_S, "mute", TE_G3);
        } else {                                   /* its loop length over its level */
            fmt_int(b, (int32_t)len);
            cv_text(238 - text_w(&FONT_S, b), 0, &FONT_S, b, l < DG_COUNT && !dg_len(l) ? TE_G3 : TE_G4);
            cv_rect(208, 14, 30, 2, TE_G1);
            cv_rect(208, 14, (int32_t)(level * 30u / 127u), 2, col);
        }
        cv_blit(0, 40 + l * MAIN_ROW);
    }
    {   /* KNOB 1 swing (the global groove), 2 level, 3 steps, 4 pan of the selected track */
        track_t *t = TSEL;
        static char v[4][8];
        static const char *const lab[4] = {"swing", "level", "steps", "pan"};
        const char *val[4] = {v[0], v[1], v[2], v[3]};
        int32_t ratio[4];
        uint32_t lvl = is_drum(t) ? song.g[G_DRLVL] : t->p[P_LEVEL];
        swing_str(v[0], song.g[G_SWING]);
        fmt_int(v[1], t->p[P_MUTE] ? 0 : (int32_t)lvl * 100 / 127);
        fmt_int(v[2], t->p[P_SLEN]);
        fmt_int(v[3], t->p[P_PAN]);
        ratio[0] = song.g[G_SWING] * 10;
        ratio[1] = t->p[P_MUTE] ? 0 : (int32_t)lvl * 1000 / 127;
        ratio[2] = (t->p[P_SLEN] - 1) * 1000 / 63;
        ratio[3] = (t->p[P_PAN] + 64) * 1000 / 127;
        te_dials(184, lab, val, ratio, song.sel, &footer);
    }
}

/* ---------------------------------------------------------------- DRUMS --- */
static const char *const LV_NAME[4] = {"norm", "ghost", "soft", "hard"};
static uint32_t lvl_rank(uint32_t lvl);                 /* (below) ghost 0, soft 1, norm 2, hard 3 */
static uint16_t lvl_col(uint32_t lvl)                   /* a hit's colour by its level */
{
    return lvl == LV_GHOST ? TE_DIM[3] : lvl == LV_SOFT ? TE_MID[3] : lvl == LV_HARD ? C_WHITE : TE_DRUM;
}
static uint16_t pad_lit[DRUM_LANES];                   /* pads and key LEDs: frames left lit */
static uint8_t key_lit[27];                             /* menu NOTES, a synth track: frames its key stays lit */
static uint8_t key_lit_trk = 0xFF;                      /* (the track key_lit is for) */
static void pads_tick(void)                             /* once a frame: the hits since the last one */
{
    uint32_t i, hits, n[NPART][4], k, w;
    fm1_irq_off();
    hits = drums.hits;
    drums.hits = 0;
    for (i = 0; i < NPART; i++)
        for (w = 0; w < 4u; w++) {
            n[i][w] = note_hits[i][w];
            note_hits[i][w] = 0;
        }
    fm1_irq_on();
    for (i = 0; i < DRUM_LANES; i++) {
        if ((hits >> i) & 1u) pad_lit[i] = 6;
        else if (pad_lit[i]) pad_lit[i]--;
    }
    /* a synth track: each note it started lights its key 6 frames (~0.1 s), so a short sequencer note
     * (a 1/16 at GATE 50 %: 60..80 ms) is seen even when it ends between two frames */
    if (song.sel != key_lit_trk) {
        memset(key_lit, 0, sizeof key_lit);
        key_lit_trk = song.sel;
    }
    for (k = 0; k < 27u; k++)
        if (key_lit[k])
            key_lit[k]--;
    if (song.sel < NPART && (n[song.sel][0] | n[song.sel][1] | n[song.sel][2] | n[song.sel][3]))
        for (k = 0; k < 27u; k++) {
            uint32_t note = kb_map(TSEL, k);
            if (note < 128u && (n[song.sel][note >> 5] >> (note & 31u)) & 1u)
                key_lit[k] = 6;
        }
}

static void drum_screen_draw(void)
{
    static uint32_t head, title_sig, body_sig, footer;
    uint32_t i, j, len = (uint32_t)clamp(TDRUM->p[P_SLEN], 1, 64), kit = drum_kit(), sig, bank;
    if (drum_cursor >= len) drum_cursor = (uint8_t)(len - 1u);
    bank = drum_cursor / 16u;
    {
        char st[16], nm[9];
        const char *style = DRUM_KIT_STYLES[kit];
        if (kit >= DRUM_SYN) {                          /* SYN1..4: the name you gave it (editor DRUM SYNTH) */
            memcpy(nm, dsu_kit(kit - DRUM_SYN)->name, 8);
            nm[8] = 0;
            if (nm[0])
                style = nm;
        }
        te_lower(st, style, sizeof st);
        st[12] = 0;
        te_header(st, TE_DRUM, &head);
    }
    sig = kit * 131u + drum_page * 7u + drum_lane * 977u + bank * 31u + len;   /* title band */
    if (ui.force || sig != title_sig) {
        char b[8];
        title_sig = sig;
        cv_begin(240, 44, C_BLACK);
        cv_rect(2, 4, 34, 34, TE_DRUM);
        fmt_int(b, (int32_t)kit + 1);
        cv_text(19 - text_w(&FONT_S, b) / 2, 13, &FONT_S, b, C_BLACK);
        cv_text(44, 6, &FONT_L, DRUM_KIT_NAMES[kit], C_WHITE);
        {
            static const char *const PG[DRUM_PAGES] = {"grid", "kit", "grp"};
            uint32_t q;
            cv_text(200, 4, &FONT_S, PG[drum_page % DRUM_PAGES], C_WHITE);
            for (q = 0; q < DRUM_PAGES; q++)                /* the pages, SEQ / EDIT / SELECT go round them */
                cv_rect(200 + (int32_t)q * 10, 26, 7, 4, q == drum_page ? TE_DRUM : TE_G2);
        }
        cv_blit(0, 40);
    }
    sig = drum_page + drum_lane * 7u + drum_cursor * 101u + song.playing * 71u + len * 3u;
    if (song.playing) sig = sig * 31u + TDRUM->seq_idx;
    for (i = 0; i < DRUM_LANES; i++) sig = sig * 3u + (pad_lit[i] != 0);
    for (i = 0; i < len; i++) {
        const dstep_t *s = &TDRUM->dstep[i];
        sig = sig * 31u + dstep_mask(s);
        sig = sig * 31u + s->lvl[0] + s->lvl[1] * 7u + s->lvl[2] * 49u + s->lvl[3] * 343u;
        sig = sig * 31u + s->rat[0] + s->rat[1] * 7u + s->rat[2] * 49u + s->rat[3] * 343u;
    }
    if (drum_page == 2u) {                              /* GROUPS: loops past LEN, where each one is, mutes */
        for (i = len; i < NSTEP; i++)
            sig = sig * 31u + dstep_mask(&TDRUM->dstep[i]);
        for (i = 0; i < DG_COUNT; i++)
            sig = sig * 31u + (uint32_t)TDRUM->p[P_DGLEN + i];
        sig = sig * 31u + drum_mute + drum_grp * 13u + (song.playing ? TDRUM->seq_abs : 0u);
    }
    if (ui.force || sig != body_sig) {
        body_sig = sig;
        cv_begin(240, 100, C_BLACK);
        if (!drum_page) {                              /* GRID: the 16 lanes x 16 steps of this bank */
            for (i = 0; i < DRUM_LANES; i++) {
                int32_t y = (int32_t)i * 6 + 2;
                cv_rect(2, y, 6, 5, pad_lit[i] ? C_WHITE : i == drum_lane ? TE_DRUM : TE_G2);
                for (j = 0; j < 16u; j++) {
                    uint32_t p = bank * 16u + j;
                    const dstep_t *s = &TDRUM->dstep[p < NSTEP ? p : 0];
                    uint16_t c;
                    if (p >= len) continue;
                    c = dstep_has(s, i) ? lvl_col(dstep_lvl(s, i)) : i == drum_lane ? TE_G2 : TE_G1;
                    if (song.playing && p == TDRUM->seq_idx && c == TE_G1) c = TE_G2;
                    cv_rect(12 + (int32_t)j * 14, y, 12, 5, c);
                    if (dstep_has(s, i) && dstep_rat(s, i)) {   /* a ratchet: a notch per extra hit */
                        uint32_t r;
                        for (r = 0; r < dstep_rat(s, i); r++)
                            cv_rect(13 + (int32_t)j * 14 + (int32_t)r * 3, y + 2, 2, 1, C_BLACK);
                    }
                    if (p == drum_cursor && i == drum_lane) {
                        cv_rect(11 + (int32_t)j * 14, y - 1, 14, 1, C_WHITE);
                        cv_rect(11 + (int32_t)j * 14, y + 5, 14, 1, C_WHITE);
                    }
                }
            }
        } else if (drum_page == 1u) {                  /* KIT: 16 pads, lit on each hit */
            for (i = 0; i < DRUM_LANES; i++) {
                int32_t x = 2 + (int32_t)(i % 4u) * 60, y = (int32_t)(i / 4u) * 25;
                cv_rect(x, y, 56, 23, pad_lit[i] ? TE_DRUM : TE_G1);
                te_text_c(x + 28, y + 4, LANE_SHORT[i], pad_lit[i] ? C_BLACK : i == drum_lane ? C_WHITE : TE_G3);
            }
        } else {                                       /* GROUPS: a row each, its loop with the playhead, its level */
            for (i = 0; i < DG_COUNT; i++) {
                int32_t y = (int32_t)i * 25, h = (int32_t)(dg_level(i) * 16u / 127u);
                uint32_t gl = dg_len(i), glen = gl ? gl : len, m = dg_lanes(i), muted = !(m & ~(uint32_t)drum_mute);
                uint32_t pos = song.playing && TDRUM->seq_abs != SEQ_NONE ? TDRUM->seq_abs % glen : 0xFFu;
                uint32_t page = pos != 0xFFu ? pos / 16u : 0u;
                char nm[8], b[6];
                if (i == drum_grp)
                    cv_rect(0, y, 240, 24, TE_G1);
                te_lower(nm, DG_NAME[i], sizeof nm);
                cv_text(4, y + 4, &FONT_S, nm, muted ? TE_G3 : i == drum_grp ? C_WHITE : TE_G4);
                for (j = 0; j < 16u && page * 16u + j < glen; j++) {
                    uint32_t p = page * 16u + j;
                    uint16_t c = (dstep_mask(&TDRUM->dstep[p]) & m) ?(muted ? TE_DIM[3] : TE_DRUM) : TE_G2;
                    if (p == pos)
                        c = c == TE_G2 ? TE_G3 : C_WHITE;
                    cv_rect(62 + (int32_t)j * 8, y + 7, 7, 10, c);
                }
                fmt_int(b, (int32_t)glen);                 /* its loop: white its own, grey the track's */
                cv_text(196, y + 4, &FONT_S, b, gl ? C_WHITE : TE_G3);
                if (h > 0)
                    cv_rect(228, y + 4 + 16 - h, 6, h, muted ? TE_G2 : TE_DRUM);
            }
        }
        cv_blit(0, 84);
    }
    {
        static char v[4][12];
        const char *val[4] = {v[0], v[1], v[2], v[3]};
        static const char *const LG[4] = {"sound", "step", "hit", "level"}, *const LK[4] = {"kit", "level", "reverb", "pan"};
        int32_t ratio[4];
        if (!drum_page) {
            const dstep_t *s = &TDRUM->dstep[drum_cursor];
            str_cpy(v[0], LANE_SHORT[drum_lane], 8);
            fmt_int(v[1], drum_cursor + 1);
            str_cpy(v[2], dstep_has(s, drum_lane) ? "on" : "--", 4);
            str_cpy(v[3], dstep_has(s, drum_lane) ? LV_NAME[dstep_lvl(s, drum_lane)] : "--", 8);
            ratio[0] = (int32_t)drum_lane * 1000 / (DRUM_LANES - 1);
            ratio[1] = (int32_t)drum_cursor * 1000 / (int32_t)(len > 1u ? len - 1u : 1u);
            ratio[2] = v[2][0] == 'o' ? 1000 : 0;
            ratio[3] = dstep_has(s, drum_lane) ? (int32_t)lvl_rank(dstep_lvl(s, drum_lane)) * 333 : 0;   /* ghost left ..
                                                                     * hard right (2.5: it followed the stored order) */
            te_dials(184, LG, val, ratio, 1u, &footer);
        } else if (drum_page == 2u) {                  /* GROUPS: the group, its loop, level and mute */
            static const char *const LP[4] = {"group", "length", "level", "mute"};
            uint32_t gl = dg_len(drum_grp), lv = dg_level(drum_grp), muted = !(dg_lanes(drum_grp) & ~(uint32_t)drum_mute);
            te_lower(v[0], DG_NAME[drum_grp], 8);
            if (gl)
                fmt_int(v[1], (int32_t)gl);
            else
                str_cpy(v[1], "trk", 4);                    /* (the track's LEN) */
            fmt_int(v[2], (int32_t)(lv * 100u / 127u));
            str_cpy(v[3], muted ? "on" : "off", 4);
            ratio[0] = (int32_t)drum_grp * 1000 / (DG_COUNT - 1);
            ratio[1] = (int32_t)gl * 1000 / NSTEP;
            ratio[2] = (int32_t)lv * 1000 / 127;
            ratio[3] = muted ? 1000 : 0;
            te_dials(184, LP, val, ratio, 3u, &footer);
        } else {
            fmt_int(v[0], (int32_t)kit + 1);
            fmt_int(v[1], song.g[G_DRLVL] * 100 / 127);
            fmt_int(v[2], song.g[G_DRREV] * 100 / 127);
            fmt_int(v[3], TDRUM->p[P_PAN]);
            ratio[0] = (int32_t)kit * 1000 / (int32_t)(DRUM_KITS - 1u);
            ratio[1] = song.g[G_DRLVL] * 1000 / 127;
            ratio[2] = song.g[G_DRREV] * 1000 / 127;
            ratio[3] = (TDRUM->p[P_PAN] + 64) * 1000 / 127;
            te_dials(184, LK, val, ratio, 2u, &footer);
        }
    }
}

/* the level order of the knobs: ghost < soft < norm < hard */
static const uint8_t LV_UP[4] = {LV_GHOST, LV_SOFT, LV_NORM, LV_HARD};
static uint32_t lvl_rank(uint32_t lvl) { return lvl == LV_GHOST ? 0u : lvl == LV_SOFT ? 1u : lvl == LV_NORM ? 2u : 3u; }

/* the GRID page shown, no layer held: its keys are steps (seq.c kb_grid) */
static int grid_keys_on(void) { return on_drum_page() && !drum_page && song.sel == TRK_DRUM && !ui.menu; }
/* a key down on the GRID page: the white keys are the 16 steps of this page of steps for the sound of
 * KNOB 1 — an empty one is set (NORM, the sound heard), a set one cleared; the first four black keys
 * pick the page (steps 1-16, 17-32, 33-48, 49-64), as in the SEQ layer. The cursor follows */
static void grid_key(uint32_t k)
{
    static const int8_t PG[12] = {-1, 0, -1, 1, -1, 2, -1, -1, 3, -1, -1, -1};
    uint32_t len = trk_len(TDRUM), idx;
    int32_t w = punch_key(k);
    dstep_t *st;
    if (w < 0) {
        if (k < 12u && PG[k] >= 0 && (uint32_t)PG[k] * 16u < len) {
            idx = (uint32_t)PG[k] * 16u + drum_cursor % 16u;
            drum_cursor = (uint8_t)(idx < len ? idx : len - 1u);
            ui.force = 1;
        }
        return;
    }
    idx = drum_cursor / 16u * 16u + (uint32_t)w;
    if (idx >= len)
        return;
    drum_cursor = (uint8_t)idx;
    if (song.playing && arrangement_enabled) {
        ui_message("STOP THE SONG FIRST");
        return;
    }
    undo_mark(TDRUM, ui.step_sess ? ui.step_sess : (ui.step_sess = (undo_sess += 4u) | 3u));
    st = &TDRUM->dstep[idx];
    fm1_irq_off();
    if (dstep_has(st, drum_lane)) {
        dstep_clr(st, drum_lane);
        if (!dstep_mask(st)) {                          /* an empty step keeps no lock, no nudge, no condition */
            lock_del(TDRUM, idx, P_COUNT);
            TDRUM->micro[idx] = 0;
            step_fill_set(TDRUM, idx, FC_NORM);
        }
    } else {
        dstep_set(st, drum_lane, LV_NORM, 0);
        TDRUM->seq_active = 1;
    }
    fm1_irq_on();
    if (dstep_has(st, drum_lane))
        audition_lane(drum_lane);
    sync_reload = 1;
}

static void drum_screen_input(uint32_t pressed, uint32_t home)
{
    uint32_t k, b;
    int32_t s;
    if (song.sel != TRK_DRUM || home == 1u) {
        go_home();
        return;
    }
    for (k = 0; k < NB; k++) if ((pressed >> panel.btn[k]) & 1u) {
        b = k;
        if (b == B_PLAY) {
            if (ft_owns_press()) ;
            else if (!song.playing && arrangement_enabled && !arr_valid(&arrangement, arrangement_ready())) ui_message("EMPTY SECTION: REC");
            else transport_req = song.playing ? 2 : 1;
        } else if (b == B_SEQ || b == B_EDIT) {
            drum_page = (uint8_t)((drum_page + 1u) % DRUM_PAGES);
            ui.msg_t = 0;
            ui.force = 1;
        } else if (b == B_SAVE) {
            studio_open(SC_SONG);
            return;
        }
    }
    if ((s = panel_enc(EN_SELECT))) {                   /* SELECT: grid <- kit -> groups (its pages) */
        uint8_t pg = (uint8_t)clamp((int32_t)drum_page + (s > 0 ? 1 : -1), 0, DRUM_PAGES - 1);
        if (pg != drum_page) {
            drum_page = pg;
            ui.msg_t = 0;
            ui.force = 1;
        }
    }
    if ((s = panel_enc(EN_ALGO)) && !ft_on) {
        lane_step(s);                                  /* (HYPNO-1: through the groups, then the synth tracks) */
        if (song.sel != TRK_DRUM) {
            go_home();
            return;
        }
    }
    if ((s = panel_enc(EN_PRESET))) TDRUM->p[P_E0] = (int16_t)clamp(TDRUM->p[P_E0] + s, 0, DRUM_KITS - 1);
    for (k = 0; k < 4u; k++) if ((s = panel_enc(EN_K1 + k))) {
        ui.hot_col = (uint8_t)k;
        ui.hot_t = 40;
        if (!drum_page) {
            dstep_t *st = &TDRUM->dstep[drum_cursor];
            if (k == 0) {                              /* the sound: heard */
                uint8_t l = (uint8_t)clamp(drum_lane + s, 0, DRUM_LANES - 1);
                if (l != drum_lane) audition_lane(l);
                drum_lane = l;
            }
            if (k == 1) {                              /* the step: what it holds, heard */
                uint8_t c = (uint8_t)clamp(drum_cursor + s, 0, TDRUM->p[P_SLEN] - 1);
                if (c != drum_cursor) audition_step(&TDRUM->dstep[c]);
                drum_cursor = c;
            }
            if (k >= 2) {
                if (song.playing && arrangement_enabled) { ui_message("STOP THE SONG FIRST"); continue; }
                undo_mark(TDRUM, ui.step_sess ? ui.step_sess : (ui.step_sess = (undo_sess += 4u) | 3u));
                fm1_irq_off();
                if (k == 2) {
                    if (s > 0) dstep_set(st, drum_lane, LV_NORM, 0);
                    else dstep_clr(st, drum_lane);
                } else if (dstep_has(st, drum_lane)) {
                    uint32_t r = (uint32_t)clamp((int32_t)lvl_rank(dstep_lvl(st, drum_lane)) + (s > 0 ? 1 : -1), 0, 3);
                    dstep_set(st, drum_lane, LV_UP[r], dstep_rat(st, drum_lane));
                }
                fm1_irq_on();
                sync_reload = 1;
            }
        } else if (drum_page == 2u) {                  /* GROUPS */
            uint32_t g = drum_grp, m = dg_lanes(g);
            if (k == 0)
                drum_grp = (uint8_t)clamp((int32_t)drum_grp + s, 0, DG_COUNT - 1);
            if (k == 1) {                              /* from the track's LEN up or down; below 1: the track's again */
                int32_t n = (int32_t)(dg_len(g) ? dg_len(g) : trk_len(TDRUM)) + s;
                dg_set(g, n < 1 ? 0 : n, (int32_t)dg_level(g));
            }
            if (k == 2)
                dg_set(g, (int32_t)dg_len(g), (int32_t)dg_level(g) + s);
            if (k == 3)                                /* right: muted, left: heard (the live mutes, not saved) */
                drum_mute = (uint16_t)(s > 0 ? drum_mute | m : drum_mute & ~m);
            if (k == 1 || k == 2)
                sync_reload = 1;
        } else {
            if (k == 0) TDRUM->p[P_E0] = (int16_t)clamp(TDRUM->p[P_E0] + s, 0, DRUM_KITS - 1);
            if (k == 1) song.g[G_DRLVL] = (int16_t)clamp(song.g[G_DRLVL] + s, 0, 127);
            if (k == 2) song.g[G_DRREV] = (int16_t)clamp(song.g[G_DRREV] + s, 0, 127);
            if (k == 3) TDRUM->p[P_PAN] = (int16_t)clamp(TDRUM->p[P_PAN] + s, -64, 63);
        }
    }
}

/* ---------------------------------------------------------------- REC --- */
/* a big seven-segment digit, TE style: w x h, segments t thick */
static void te_digit(int32_t x, int32_t y, int32_t w, int32_t h, int32_t t, uint32_t d, uint16_t c)
{
    static const uint8_t SEG[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};   /* gfedcba */
    uint32_t m = SEG[d % 10u];
    int32_t hh = h / 2;
    if (m & 0x01) cv_rect(x + t, y, w - 2 * t, t, c);                       /* a */
    if (m & 0x02) cv_rect(x + w - t, y + t, t, hh - t, c);                  /* b */
    if (m & 0x04) cv_rect(x + w - t, y + hh, t, hh - t, c);                 /* c */
    if (m & 0x08) cv_rect(x + t, y + h - t, w - 2 * t, t, c);               /* d */
    if (m & 0x10) cv_rect(x, y + hh, t, hh - t, c);                         /* e */
    if (m & 0x20) cv_rect(x, y + t, t, hh - t, c);                          /* f */
    if (m & 0x40) cv_rect(x + t, y + hh - t / 2, w - 2 * t, t, c);          /* g */
}

static uint8_t rec_shown;
/* the four tracks, compact, from y0: the one that records is framed in red */
static void rec_rows(uint32_t rt, uint32_t take, uint32_t y0, uint32_t *cache)
{
    uint32_t sig = rt * 3u + rec_wait + take * 5u + drum_kit() * 977u + y0, i, j;
    for (i = 0; i < NTRK; i++) {
        char b[16];
        if (i == TRK_DRUM) str_cpy(b, DRUM_KIT_NAMES[drum_kit()], sizeof b);
        else trk_short_name(i, b);
        sig = studio_hash(sig, b) + (uint32_t)trk[i].p[P_SLEN] * 31u;
        for (j = 0; j < NSTEP; j++) sig = sig * 3u + (uint32_t)trk_step_on(&trk[i], j);
    }
    if (!ui.force && sig == *cache)
        return;
    *cache = sig;
    cv_begin(240, 76, C_BLACK);
    for (i = 0; i < NTRK; i++) {
        const track_t *t = &trk[i];
        uint32_t len = (uint32_t)clamp(t->p[P_SLEN], 1, 64), armed = i == rt;
        int32_t y = (int32_t)i * 15;                 /* (HYPNO-1: five rows in the 76 px SLOOP gave four) */
        char b[16];
        if (armed) cv_rect(0, y, 240, 15, TE_RED), cv_rect(1, y + 1, 238, 13, C_BLACK);
        cv_rect(4, y + 2, 11, 11, armed ? TE_TRK[i] : TE_TRK_DIM[i]);
        if (i == TRK_DRUM) str_cpy(b, DRUM_KIT_NAMES[drum_kit()], sizeof b);
        else trk_short_name(i, b);
        b[10] = 0;
        cv_text(22, y, &FONT_S, b, armed ? C_WHITE : TE_G3);
        for (j = 0; j < 16u; j++) {
            uint32_t a = j * len / 16u, z = (j + 1u) * len / 16u, k, on = 0;
            if (z == a) z = a + 1u;
            for (k = a; k < z && k < NSTEP; k++) if (trk_step_on(t, k)) on = 1;
            cv_rect(106 + (int32_t)j * 8, y + 3, 6, 9, on ? (armed ? TE_TRK[i] : TE_TRK_DIM[i]) : TE_G1);
        }
    }
    cv_blit(0, y0);
}

/* REC armed (stopped: waiting for the first note, or PLAY), its count-in, or a free take running
 * (seq.c). Armed, the dials say how it records: KNOB 1 MODE free / tempo (an empty project),
 * KNOB 2 LENGTH (1, 2 or 4 bars), KNOB 3 START note / count (ui_input.c rec_knobs). The five tracks
 * stay in view; the REC LED blinks while armed, is lit during the take. */
static void rec_screen_draw(void)
{
    static uint32_t head, body, rows, foot, dials;
    static uint8_t layout;
    uint32_t sig, take = ft_on, empty = take || project_empty(), rt = take ? ft_trk % NTRK : song.sel;
    uint32_t blink = (fm1_ms / 250u) & 1u, secs = take ? ft_t * CTL / FS : 0u, bars = 0, bpm = 0;
    uint32_t lay = take ? 1u : 2u, free = empty && !rec_tempo && !take, count = ci_on;
    if (take)
        bars = ft_fit(ft_t, &bpm);
    if (!rec_shown || lay != layout) {
        lcd_fill(0, 0, 240, 240, C_BLACK);
        ui.force = 1;
    }
    rec_shown = 1;
    layout = (uint8_t)lay;
    te_header(take ? "free take" : count ? "count-in" : "rec ready", TE_RED, &head);
    if (take) {                                         /* free take: the time, the loop it makes */
        sig = 1000003u + rt * 7919u + blink * 31u + secs * 131u + bars * 17u + bpm * 3u;
        if (ui.force || sig != body) {
            char b[24];
            uint32_t n = secs > 99u ? 99u : secs;
            body = sig;
            cv_begin(240, 106, C_BLACK);
            te_disc(18, 30, 9, blink ? TE_RED : TE_DIM[3]);
            if (n >= 10u)
                te_digit(36, 4, 32, 56, 7, n / 10u, C_WHITE);
            te_digit(76, 4, 32, 56, 7, n % 10u, C_WHITE);
            cv_text(112, 44, &FONT_S, "s", TE_G3);
            cv_rect(132, 6, 1, 54, TE_G1);
            if (bars) {
                fmt_int(b, (int32_t)bars);
                cv_text(146, 4, &FONT_L, b, TE_COL[rt & 3u]);
                cv_text(146 + text_w(&FONT_L, b) + 6, 18, &FONT_S, bars == 1u ? "bar" : "bars", TE_G3);
                fmt_int(b, (int32_t)bpm);
                cv_text(146, 42, &FONT_S, b, C_WHITE);
                cv_text(146 + text_w(&FONT_S, b) + 4, 42, &FONT_S, "bpm", TE_G3);
            } else {
                cv_text(146, 4, &FONT_L, "-", TE_G3);
            }
            te_text_c(120, 80, "press rec on the 1", TE_RED);
            cv_blit(0, 42);
        }
        rec_rows(rt, take, 148u, &rows);
        if (ui.force || foot != 1u) {
            foot = 1u;
            cv_begin(240, 16, C_BLACK);
            cv_text(4, 0, &FONT_S, "rec: close", TE_G3);
            cv_text(236 - text_w(&FONT_S, "play: drop"), 0, &FONT_S, "play: drop", TE_G3);
            cv_blit(0, 224);
        }
        return;
    }
    foot = 0;
    /* armed / counting in: what happens next */
    sig = 2000003u + empty * 7u + free * 11u + rec_count * 13u + count * 17u + ci_beat * 19u + blink * 31u;
    if (ui.force || sig != body) {
        static const char *const L1[3] = {"play freely", "play a note", "press play"};
        static const char *const L2[3] = {"then rec on the 1", "it starts the loop", "4 clicks, then rec"};
        static const char *const L3[3] = {"the tempo follows you", "play: go  rec: cancel", "rec: cancel"};
        uint32_t m = free ? 0u : rec_count ? 2u : 1u;
        body = sig;
        cv_begin(240, 64, C_BLACK);
        if (count) {                                    /* the count-in: 4, 3, 2, 1 */
            te_digit(96, 4, 30, 54, 6, 4u - (ci_beat > 3u ? 3u : ci_beat), C_WHITE);
            te_disc(60, 31, 12, TE_RED);
            cv_text(144, 14, &FONT_S, "count-in", TE_G4);
            cv_text(144, 34, &FONT_S, "rec: cancel", TE_G3);
        } else {
            te_disc(28, 30, 18, blink ? TE_RED : TE_DIM[3]);
            te_disc(28, 30, 7, C_BLACK);
            cv_text(60, 6, &FONT_S, L1[m], C_WHITE);
            cv_text(60, 24, &FONT_S, L2[m], TE_G4);
            cv_text(60, 42, &FONT_S, L3[m], TE_G3);
        }
        cv_blit(0, 42);
    }
    rec_rows(rt, 0u, 106u, &rows);
    {   /* the dials: how it records */
        static char v[3][10];
        static const char *const LAB_E[4] = {"mode", "length", "start", ""};
        static const char *const LAB_F[4] = {"mode", "", "", ""};
        static const char *const LAB_N[4] = {"", "length", "start", ""};
        static const char *const LAB_0[4] = {"", "", "", ""};
        const char *val[4] = {v[0], v[1], v[2], ""};
        int32_t ratio[4] = {0, 0, 0, 0};
        uint32_t len = (uint32_t)clamp(TSEL->p[P_SLEN], 1, 64);
        str_cpy(v[0], rec_tempo ? "tempo" : "free", sizeof v[0]);
        if (len % 16u == 0u) {
            fmt_int(v[1], (int32_t)(len / 16u));
            str_cpy(v[1] + str_len(v[1]), len == 16u ? " bar" : " bars", 6);
        } else {
            fmt_int(v[1], (int32_t)len);
            str_cpy(v[1] + str_len(v[1]), " st", 4);
        }
        str_cpy(v[2], rec_count ? "count" : "note", sizeof v[2]);
        ratio[0] = rec_tempo ? 1000 : 0;
        ratio[1] = (int32_t)(len - 1u) * 1000 / 63;
        ratio[2] = rec_count ? 1000 : 0;
        {
            const char *const *lab = count ? LAB_0 : !empty ? LAB_N : free ? LAB_F : LAB_E;
            uint32_t k;
            for (k = 0; k < 4u; k++)
                if (!lab[k][0])
                    val[k] = "";                        /* (a dial not shown: no value either) */
            te_dials(184, lab, val, ratio, rt * 7u + count * 3u, &dials);
        }
    }
}
