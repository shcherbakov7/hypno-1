/* SPDX-License-Identifier: GPL-3.0-only */
/* HYPNO-1 boot splash, drawn (no bitmap: SLOOP's logo took 4.6 KB of flash): concentric rings in acid green round a
 * black eye, the name over them, the version and where it comes from under them. In bands (the canvas holds 124
 * rows), each band its own part of the same rings */
#define SPL_CX 120
#define SPL_CY 92
static void splash_rings(uint32_t y0, uint32_t rows)
{
    int32_t x, y;
    cv_begin(240, rows, C_BLACK);
    for (y = 0; y < (int32_t)rows; y++)
        for (x = 0; x < 240; x++) {
            int32_t dx = x - SPL_CX, dy = (int32_t)y0 + y - SPL_CY, d2 = dx * dx + dy * dy, r = 0;
            uint16_t c;
            while ((r + 1) * (r + 1) * 64 <= d2 && r < 12)   /* the ring: every 8 px */
                r++;
            if (r >= 11 || r < 2)
                continue;
            c = (r & 1) ? RGB(182, 255, 46) : RGB(0, 0, 0);
            if ((r & 1) && r > 6)                       /* fading out */
                c = r > 8 ? RGB(40, 58, 10) : RGB(96, 136, 24);
            cv_pset(x, y, c);
        }
}
/* the boot screen */
static void sloop_splash(void)
{
    const char *v = FELUCCA_VERSION;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    splash_rings(0, 120);
    cv_blit(0, 0);
    splash_rings(120, 64);
    {   /* the name on a black plate over the rings' lower half */
        int32_t w = text_w(&FONT_L, "HYPNO-1");
        cv_rect(120 - w / 2 - 6, 10, w + 12, 34, C_BLACK);
        cv_text(120 - w / 2, 12, &FONT_L, "HYPNO-1", C_WHITE);
    }
    cv_blit(0, 120);
    cv_begin(240, 52, C_BLACK);                         /* (FELUCCA_VERSION stays SLOOP's: the editor reads it) */
    cv_text(120 - text_w(&FONT_S, "raw hypnotic acid techno") / 2, 0, &FONT_S, "raw hypnotic acid techno", RGB(182, 255, 46));
    {
        char b[24];
        str_cpy(b, "on ", sizeof b);
        str_cpy(b + 3, v, sizeof b - 3u);
        te_lower(b, b, sizeof b);
        cv_text(120 - text_w(&FONT_S, b) / 2, 18, &FONT_S, b, RGB(196, 196, 204));
    }
    cv_text(120 - text_w(&FONT_S, "based on felucca") / 2, 34, &FONT_S, "based on felucca", RGB(96, 96, 104));
    cv_blit(0, 186);
}
