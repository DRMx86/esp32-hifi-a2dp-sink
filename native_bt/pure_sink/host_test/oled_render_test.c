/* Host test for main/oled_render.c (no ESP32 needed).
 *
 *   cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I../main oled_render_test.c -o oled_render_test
 *   ./oled_render_test              run the checks
 *   ./oled_render_test OUTDIR       also write each screen as a PBM image (P1) into OUTDIR
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../main/oled_render.c" /* include the .c to reach its static helpers */

#define PAGES 8u
static int failures;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static const char *out_dir;

static void write_pbm(const char *name, const uint8_t *fb, unsigned pages) {
    if (!out_dir) return;
    char path[256];
    snprintf(path, sizeof path, "%s/%s.pbm", out_dir, name);
    FILE *f = fopen(path, "w");
    if (!f) { printf("cannot write %s\n", path); ++failures; return; }
    fprintf(f, "P1\n%u %u\n", OLED_WIDTH, pages * 8u);
    for (unsigned y = 0; y < pages * 8u; ++y) {
        for (unsigned x = 0; x < OLED_WIDTH; ++x) fputc((fb[(y / 8u) * OLED_WIDTH + x] >> (y % 8u)) & 1u ? '1' : '0', f);
        fputc('\n', f);
    }
    fclose(f);
}

static unsigned lit_pixels(const uint8_t *fb, unsigned pages) {
    unsigned n = 0;
    for (unsigned i = 0; i < pages * OLED_WIDTH; ++i) n += (unsigned)__builtin_popcount(fb[i]);
    return n;
}

/* True if any pixel is lit in the given page range. */
static bool page_has_pixels(const uint8_t *fb, unsigned first, unsigned last) {
    for (unsigned p = first; p <= last; ++p)
        for (unsigned x = 0; x < OLED_WIDTH; ++x)
            if (fb[p * OLED_WIDTH + x]) return true;
    return false;
}

static void test_font(void) {
    for (unsigned g = 0; g < GLYPH_COUNT; ++g) {
        unsigned lit = 0;
        for (unsigned r = 0; r < 7; ++r) {
            CHECK(font_rows[g][r] < 0x20); /* 5 bits per row */
            lit += (unsigned)__builtin_popcount(font_rows[g][r]);
        }
        if (g == GLYPH_SPACE) CHECK(lit == 0);
        else CHECK(lit > 0);
    }
    /* No two printable glyphs may be identical (catches copy/paste slips in the table). */
    for (unsigned a = 1; a < GLYPH_COUNT; ++a)
        for (unsigned b = a + 1; b < GLYPH_COUNT; ++b) CHECK(memcmp(font_rows[a], font_rows[b], 7) != 0);
    if (out_dir) { /* contact sheet of the whole font, 16 glyphs per row */
        char path[256];
        snprintf(path, sizeof path, "%s/font.pbm", out_dir);
        FILE *f = fopen(path, "w");
        if (f) {
            const unsigned per_row = 16, rows = (GLYPH_COUNT + per_row - 1) / per_row;
            fprintf(f, "P1\n%u %u\n", per_row * 6, rows * 8);
            for (unsigned y = 0; y < rows * 8; ++y)
                for (unsigned x = 0; x < per_row * 6; ++x) {
                    unsigned g = (y / 8) * per_row + x / 6, c = x % 6, r = y % 8;
                    int on = g < GLYPH_COUNT && c < 5 && r < 7 && ((font_rows[g][r] >> (4 - c)) & 1);
                    fputc(on ? '1' : '0', f);
                    if (x + 1 == per_row * 6) fputc('\n', f);
                }
            fclose(f);
        }
    }
}

static void test_text(void) {
    oled_line_t l;
    line_set(&l, "Abc 09");
    CHECK(l.n == 6 && l.g[0] == 'A' - 0x20 && l.g[3] == GLYPH_SPACE && l.g[5] == '9' - 0x20);

    line_set(&l, "Caf\xc3\xa9 \xc3\x9c" "ber"); /* Café Über */
    CHECK(l.n == 9 && l.g[3] == 'e' - 0x20 && l.g[5] == 'U' - 0x20);

    line_set(&l, "\xe2\x80\x99n\xe2\x80\x94n"); /* ’n—n */
    CHECK(l.n == 4 && l.g[0] == '\'' - 0x20 && l.g[2] == '-' - 0x20);

    line_set(&l, "\xe6\x97\xa5\xe6\x9c\xac"); /* two CJK characters: one '?' each */
    CHECK(l.n == 2 && l.g[0] == GLYPH_QUESTION && l.g[1] == GLYPH_QUESTION);

    /* Malformed input must neither overrun the string nor loop. */
    line_set(&l, "ab\xe2\x80"); /* sequence cut off by the terminator */
    CHECK(l.n == 4 && l.g[2] == GLYPH_QUESTION && l.g[3] == GLYPH_QUESTION);
    line_set(&l, "\xff\xfe\x80z");
    CHECK(l.n == 4 && l.g[3] == 'z' - 0x20);
    line_set(&l, "\xc0\x80"); /* overlong NUL */
    CHECK(l.n == 2);

    /* Longer than the line capacity: truncated, never overflows. */
    char big[300];
    memset(big, 'x', sizeof big - 1);
    big[sizeof big - 1] = 0;
    line_set(&l, big);
    CHECK(l.n == OLED_TEXT_BYTES);
}

static void test_marquee(void) {
    const unsigned cycle = 400, speed = 36;
    CHECK(marquee_offset(cycle, speed, 0) == 0);
    CHECK(marquee_offset(cycle, speed, PAUSE_MS - 1) == 0);
    CHECK(marquee_offset(cycle, speed, PAUSE_MS + 1000) == speed); /* one second of scrolling */
    uint32_t period = PAUSE_MS + cycle * 1000u / speed;
    CHECK(marquee_offset(cycle, speed, period) == 0); /* wraps back to the pause */
    CHECK(marquee_offset(cycle, speed, 5u * period + 10u) == 0);
    for (uint32_t t = 0; t < 3u * period; t += 7u) CHECK(marquee_offset(cycle, speed, t) < cycle);
    CHECK(marquee_offset(cycle, speed, 0xffffffffu) < cycle); /* clock near wrap */
}

static oled_info_t base_info(void) {
    oled_info_t i;
    memset(&i, 0, sizeof i);
    i.link = OLED_LINK_STREAMING;
    i.play_status = 1;
    i.volume = 100;
    i.rate = 96000;
    i.sample_bytes = 3;
    strcpy(i.codec, "LDAC");
    return i;
}

static void test_scenes(void) {
    static uint8_t fb[8 * OLED_WIDTH];
    oled_scene_t s;
    oled_scene_init(&s, "HiFi DM32");
    oled_info_t i;
    memset(&i, 0, sizeof i);
    i.play_status = OLED_PLAY_UNKNOWN;

    /* Idle: device name big, waiting text, no status bar. */
    CHECK(oled_scene_update(&s, &i, 0));
    CHECK(!oled_scene_update(&s, &i, 5)); /* unchanged input: nothing to redraw */
    oled_scene_draw(&s, fb, PAGES, 0);
    CHECK(!page_has_pixels(fb, 0, 1));
    CHECK(page_has_pixels(fb, 2, 3));
    CHECK(page_has_pixels(fb, 5, 5));
    CHECK(!oled_scene_animating(&s, PAGES));
    write_pbm("idle", fb, PAGES);
    i.pairing = true;
    CHECK(oled_scene_update(&s, &i, 10));
    oled_scene_draw(&s, fb, PAGES, 10);
    write_pbm("pairing", fb, PAGES);

    /* Connected, phone has not sent metadata. */
    i = base_info();
    i.rate = 0;
    i.sample_bytes = 0;
    i.link = OLED_LINK_CONNECTED;
    i.play_status = OLED_PLAY_UNKNOWN;
    CHECK(oled_scene_update(&s, &i, 20));
    oled_scene_draw(&s, fb, PAGES, 20);
    write_pbm("connected_no_meta", fb, PAGES);

    /* Normal metadata that fits. */
    i = base_info();
    strcpy(i.title, "Teardrop");
    strcpy(i.artist, "Massive Attack");
    strcpy(i.album, "Mezzanine");
    CHECK(oled_scene_update(&s, &i, 30));
    CHECK(!oled_scene_animating(&s, PAGES));
    oled_scene_draw(&s, fb, PAGES, 30);
    CHECK(page_has_pixels(fb, 0, 0) && page_has_pixels(fb, 2, 3) && page_has_pixels(fb, 5, 6));
    CHECK(!page_has_pixels(fb, 1, 1) && !page_has_pixels(fb, 4, 4) && !page_has_pixels(fb, 7, 7));
    write_pbm("meta_short", fb, PAGES);

    /* Long strings scroll; the scroll restarts when the text changes. */
    strcpy(i.title, "Everything In Its Right Place (Live From The Basement)");
    strcpy(i.artist, "Radiohead feat. Some Very Long Collaborating Artist Name");
    strcpy(i.album, "Kid A Mnesia: Complete Edition \xe2\x80\x94 Deluxe");
    CHECK(oled_scene_update(&s, &i, 1000));
    CHECK(oled_scene_animating(&s, PAGES));
    static uint8_t a[8 * OLED_WIDTH], b[8 * OLED_WIDTH], c[8 * OLED_WIDTH];
    oled_scene_draw(&s, a, PAGES, 1000);
    oled_scene_draw(&s, b, PAGES, 1000 + PAUSE_MS - 1);
    oled_scene_draw(&s, c, PAGES, 1000 + PAUSE_MS + 700);
    CHECK(memcmp(a, b, sizeof a) == 0); /* resting during the pause */
    CHECK(memcmp(a, c, sizeof a) != 0); /* moving afterwards */
    write_pbm("meta_long_t0", a, PAGES);
    write_pbm("meta_long_t2", c, PAGES);
    for (uint32_t t = 1000; t < 1000 + 20000; t += 37) oled_scene_draw(&s, fb, PAGES, t); /* no overrun (ASan) */

    /* Playing / paused / stopped icons differ; volume and rate show in the bar. */
    i = base_info();
    strcpy(i.title, "T");
    static uint8_t play[8 * OLED_WIDTH], pause_[8 * OLED_WIDTH];
    CHECK(oled_scene_update(&s, &i, 2000));
    oled_scene_draw(&s, play, PAGES, 2000);
    i.play_status = 2;
    CHECK(oled_scene_update(&s, &i, 2001));
    oled_scene_draw(&s, pause_, PAGES, 2001);
    CHECK(memcmp(play, pause_, OLED_WIDTH) != 0);
    write_pbm("paused", pause_, PAGES);

    /* Status text selection: widest codec name must still fit beside the volume. */
    oled_line_t left, right;
    i = base_info();
    strcpy(i.codec, "Opus (Android)");
    i.rate = 44100;
    i.sample_bytes = 2;
    i.volume = 127;
    build_status(&left, &right, &i);
    CHECK(right.n == 4); /* "100%" */
    CHECK(left.n * CELL + CELL + right.n * CELL <= OLED_WIDTH);
    oled_line_t expect;
    line_set(&expect, " ");
    i.volume = 0;
    build_status(&left, &right, &i);
    CHECK(right.n == 2); /* "0%" */
    i = base_info();
    i.rate = 88200;
    build_status(&left, &right, &i);
    CHECK(left.n == 2 + (unsigned)strlen("LDAC 88.2k/24"));
    i.rate = 0;
    i.sample_bytes = 0;
    strcpy(i.codec, "none");
    build_status(&left, &right, &i);
    CHECK(left.n == 2 + (unsigned)strlen("Connected"));
    i.link = OLED_LINK_IDLE;
    build_status(&left, &right, &i);
    CHECK(left.n == 0 && right.n == 0);
}

static void test_short_display(void) {
    static uint8_t fb[4 * OLED_WIDTH];
    oled_scene_t s;
    oled_scene_init(&s, "HiFi DM32");
    oled_info_t i = base_info();
    strcpy(i.title, "Teardrop");
    strcpy(i.artist, "Massive Attack");
    strcpy(i.album, "Mezzanine");
    (void)oled_scene_update(&s, &i, 0);
    oled_scene_draw(&s, fb, 4, 0);
    CHECK(page_has_pixels(fb, 0, 0) && page_has_pixels(fb, 1, 2) && page_has_pixels(fb, 3, 3));
    write_pbm("short_128x32", fb, 4);
    CHECK(lit_pixels(fb, 4) > 0);
}

int main(int argc, char **argv) {
    if (argc > 1) out_dir = argv[1];
    test_font();
    test_text();
    test_marquee();
    test_scenes();
    test_short_display();
    printf(failures ? "%d check(s) FAILED\n" : "all checks passed\n", failures);
    return failures ? 1 : 0;
}
