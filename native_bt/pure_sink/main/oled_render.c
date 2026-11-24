/* OLED text rendering: see oled_render.h. No ESP-IDF includes on purpose (host-testable). */
#include "oled_render.h"

#include <stdio.h>
#include <string.h>

#define CELL 6u            /* 5 pixel columns + 1 gap */
#define GAP_CELLS 3u       /* blank cells between the end and the restart of scrolling text */
#define PAUSE_MS 1500u     /* scrolling text rests at the start of each pass */
#define SPEED_1X 24u       /* scroll speed in pixels per second */
#define SPEED_2X 36u
#define ICON_PLAY 95u
#define ICON_PAUSE 96u
#define ICON_STOP 97u
#define GLYPH_COUNT 98u
#define GLYPH_SPACE 0u
#define GLYPH_QUESTION ('?' - 0x20)

/* 5x7 glyphs, one byte per row, bit 4 = leftmost pixel. Index = ASCII code - 0x20, then play/pause/stop icons. */
static const uint8_t font_rows[GLYPH_COUNT][7] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, /* ' ' */
    {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04}, /* ! */
    {0x0A, 0x0A, 0x0A, 0x00, 0x00, 0x00, 0x00}, /* " */
    {0x0A, 0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x0A}, /* # */
    {0x04, 0x0F, 0x14, 0x0E, 0x05, 0x1E, 0x04}, /* $ */
    {0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03}, /* % */
    {0x0C, 0x12, 0x14, 0x08, 0x15, 0x12, 0x0D}, /* & */
    {0x0C, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00}, /* ' */
    {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02}, /* ( */
    {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08}, /* ) */
    {0x00, 0x04, 0x15, 0x0E, 0x15, 0x04, 0x00}, /* * */
    {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00}, /* + */
    {0x00, 0x00, 0x00, 0x00, 0x0C, 0x04, 0x08}, /* , */
    {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}, /* - */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}, /* . */
    {0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x00}, /* / */
    {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}, /* 0 */
    {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}, /* 1 */
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}, /* 2 */
    {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}, /* 3 */
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, /* 4 */
    {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}, /* 5 */
    {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}, /* 6 */
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}, /* 7 */
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, /* 8 */
    {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}, /* 9 */
    {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00}, /* : */
    {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x04, 0x08}, /* ; */
    {0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02}, /* < */
    {0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00}, /* = */
    {0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08}, /* > */
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}, /* ? */
    {0x0E, 0x11, 0x01, 0x0D, 0x15, 0x15, 0x0E}, /* @ */
    {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, /* A */
    {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}, /* B */
    {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}, /* C */
    {0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C}, /* D */
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}, /* E */
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}, /* F */
    {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}, /* G */
    {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, /* H */
    {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}, /* I */
    {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}, /* J */
    {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}, /* K */
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}, /* L */
    {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}, /* M */
    {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11}, /* N */
    {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, /* O */
    {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}, /* P */
    {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}, /* Q */
    {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}, /* R */
    {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}, /* S */
    {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}, /* T */
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, /* U */
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}, /* V */
    {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}, /* W */
    {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}, /* X */
    {0x11, 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04}, /* Y */
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}, /* Z */
    {0x0E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0E}, /* [ */
    {0x00, 0x10, 0x08, 0x04, 0x02, 0x01, 0x00}, /* \ */
    {0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E}, /* ] */
    {0x04, 0x0A, 0x11, 0x00, 0x00, 0x00, 0x00}, /* ^ */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F}, /* _ */
    {0x08, 0x04, 0x02, 0x00, 0x00, 0x00, 0x00}, /* ` */
    {0x00, 0x00, 0x0E, 0x01, 0x0F, 0x11, 0x0F}, /* a */
    {0x10, 0x10, 0x16, 0x19, 0x11, 0x11, 0x1E}, /* b */
    {0x00, 0x00, 0x0E, 0x10, 0x10, 0x11, 0x0E}, /* c */
    {0x01, 0x01, 0x0D, 0x13, 0x11, 0x11, 0x0F}, /* d */
    {0x00, 0x00, 0x0E, 0x11, 0x1F, 0x10, 0x0E}, /* e */
    {0x06, 0x09, 0x08, 0x1C, 0x08, 0x08, 0x08}, /* f */
    {0x00, 0x0F, 0x11, 0x11, 0x0F, 0x01, 0x0E}, /* g */
    {0x10, 0x10, 0x16, 0x19, 0x11, 0x11, 0x11}, /* h */
    {0x04, 0x00, 0x0C, 0x04, 0x04, 0x04, 0x0E}, /* i */
    {0x02, 0x00, 0x06, 0x02, 0x02, 0x12, 0x0C}, /* j */
    {0x10, 0x10, 0x12, 0x14, 0x18, 0x14, 0x12}, /* k */
    {0x0C, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}, /* l */
    {0x00, 0x00, 0x1A, 0x15, 0x15, 0x11, 0x11}, /* m */
    {0x00, 0x00, 0x16, 0x19, 0x11, 0x11, 0x11}, /* n */
    {0x00, 0x00, 0x0E, 0x11, 0x11, 0x11, 0x0E}, /* o */
    {0x00, 0x00, 0x1E, 0x11, 0x1E, 0x10, 0x10}, /* p */
    {0x00, 0x00, 0x0D, 0x13, 0x0F, 0x01, 0x01}, /* q */
    {0x00, 0x00, 0x16, 0x19, 0x10, 0x10, 0x10}, /* r */
    {0x00, 0x00, 0x0E, 0x10, 0x0E, 0x01, 0x1E}, /* s */
    {0x08, 0x08, 0x1C, 0x08, 0x08, 0x09, 0x06}, /* t */
    {0x00, 0x00, 0x11, 0x11, 0x11, 0x13, 0x0D}, /* u */
    {0x00, 0x00, 0x11, 0x11, 0x11, 0x0A, 0x04}, /* v */
    {0x00, 0x00, 0x11, 0x11, 0x15, 0x15, 0x0A}, /* w */
    {0x00, 0x00, 0x11, 0x0A, 0x04, 0x0A, 0x11}, /* x */
    {0x00, 0x00, 0x11, 0x11, 0x0F, 0x01, 0x0E}, /* y */
    {0x00, 0x00, 0x1F, 0x02, 0x04, 0x08, 0x1F}, /* z */
    {0x02, 0x04, 0x04, 0x08, 0x04, 0x04, 0x02}, /* { */
    {0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}, /* | */
    {0x08, 0x04, 0x04, 0x02, 0x04, 0x04, 0x08}, /* } */
    {0x00, 0x00, 0x08, 0x15, 0x02, 0x00, 0x00}, /* ~ */
    {0x10, 0x18, 0x1C, 0x1E, 0x1C, 0x18, 0x10}, /* play icon */
    {0x1B, 0x1B, 0x1B, 0x1B, 0x1B, 0x1B, 0x1B}, /* pause icon */
    {0x00, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x00}, /* stop icon */
};

/* Latin-1 U+00C0..U+00FF folded to the nearest ASCII letter. */
static const char latin1_fold[64 + 1] = "AAAAAAACEEEEIIII"
                                        "DNOOOOOxOUUUUYPs"
                                        "aaaaaaaceeeeiiii"
                                        "dnooooo/ouuuuypy";
_Static_assert(sizeof(latin1_fold) == 64 + 1, "Latin-1 fold table covers U+00C0..U+00FF");

/* ------------------------------------------------------------------ text */
static uint8_t glyph_for(uint32_t cp) {
    if (cp >= 0x20 && cp <= 0x7e) return (uint8_t)(cp - 0x20);
    if (cp >= 0xc0 && cp <= 0xff) return (uint8_t)(latin1_fold[cp - 0xc0] - 0x20);
    switch (cp) {
    case 0x09: case 0xa0: return GLYPH_SPACE;
    case 0x2018: case 0x2019: return (uint8_t)('\'' - 0x20);
    case 0x201c: case 0x201d: return (uint8_t)('"' - 0x20);
    case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014: return (uint8_t)('-' - 0x20);
    case 0x2022: return (uint8_t)('*' - 0x20);
    case 0x2026: return (uint8_t)('.' - 0x20);
    default: return GLYPH_QUESTION;
    }
}

/* Append UTF-8 text as glyphs until the line holds max_n glyphs. Invalid bytes become '?'. */
static void line_append(oled_line_t *l, const char *text, unsigned max_n) {
    if (max_n > OLED_TEXT_BYTES) max_n = OLED_TEXT_BYTES;
    const uint8_t *p = (const uint8_t *)text;
    while (*p && l->n < max_n) {
        uint8_t a = *p;
        uint32_t cp = 0xfffd;
        unsigned k = 1, used = 1;
        if (a < 0x80) cp = a;
        else if (a >= 0xc2 && a <= 0xdf) { cp = a & 0x1fu; k = 2; }
        else if (a >= 0xe0 && a <= 0xef) { cp = a & 0x0fu; k = 3; }
        else if (a >= 0xf0 && a <= 0xf4) { cp = a & 0x07u; k = 4; }
        if (k > 1) {
            bool ok = true;
            for (unsigned j = 1; j < k; ++j) {
                if ((p[j] & 0xc0u) != 0x80u) { /* also stops at the terminating NUL */
                    ok = false;
                    break;
                }
                cp = (cp << 6) | (p[j] & 0x3fu);
            }
            if (ok) used = k;
            else cp = 0xfffd;
        }
        l->g[l->n++] = glyph_for(cp);
        p += used;
    }
}

static void line_set(oled_line_t *l, const char *text) {
    memset(l, 0, sizeof(*l));
    line_append(l, text, OLED_TEXT_BYTES);
}

static bool line_equal(const oled_line_t *a, const oled_line_t *b) {
    return a->n == b->n && memcmp(a->g, b->g, a->n) == 0;
}

/* Width in pixels without the trailing gap column. */
static unsigned text_px(const oled_line_t *l, unsigned scale) { return l->n ? l->n * CELL * scale - scale : 0u; }

/* ---------------------------------------------------------------- status */
static void rate_text(char *out, size_t size, uint32_t rate) {
    if (rate % 1000u == 0) snprintf(out, size, "%uk", (unsigned)(rate / 1000u));
    else snprintf(out, size, "%u.%uk", (unsigned)(rate / 1000u), (unsigned)((rate % 1000u) / 100u));
}

static uint8_t icon_for(const oled_info_t *i) {
    switch (i->play_status) {
    case 2: return ICON_PAUSE;
    case 0: return ICON_STOP;
    case OLED_PLAY_UNKNOWN: return i->link == OLED_LINK_STREAMING ? ICON_PLAY : ICON_STOP;
    default: return ICON_PLAY; /* playing, seeking */
    }
}

/* Status bar: [icon] codec rate/bits ........ volume%. Idle (no phone): empty. */
static void build_status(oled_line_t *left, oled_line_t *right, const oled_info_t *i) {
    memset(left, 0, sizeof(*left));
    memset(right, 0, sizeof(*right));
    if (i->link == OLED_LINK_IDLE) return;
    char buf[16];
    snprintf(buf, sizeof buf, "%u%%", ((unsigned)i->volume * 100u + 63u) / 127u);
    line_append(right, buf, OLED_TEXT_BYTES);
    unsigned room = (OLED_WIDTH - (right->n * CELL + CELL)) / CELL; /* glyphs left of the volume, incl. icon */
    left->g[left->n++] = icon_for(i);
    left->g[left->n++] = GLYPH_SPACE;

    const char *codec = i->codec[0] && strcmp(i->codec, "none") != 0 ? i->codec : "";
    const char *sep = codec[0] ? " " : "";
    char text[48];
    if (i->rate && i->sample_bytes) {
        char rate[16];
        rate_text(rate, sizeof rate, i->rate);
        snprintf(text, sizeof text, "%s%s%s/%u", codec, sep, rate, (unsigned)i->sample_bytes * 8u);
        if (strlen(text) + left->n > room) snprintf(text, sizeof text, "%s%s%s", codec, sep, rate);
        if (strlen(text) + left->n > room) snprintf(text, sizeof text, "%s", codec);
    } else {
        snprintf(text, sizeof text, "%s", codec[0] ? codec : "Connected");
    }
    line_append(left, text, room);
}

/* ----------------------------------------------------------------- scene */
void oled_scene_init(oled_scene_t *s, const char *device_name) {
    memset(s, 0, sizeof(*s));
    strncpy(s->name, device_name ? device_name : "", OLED_NAME_CHARS);
}

static void set_if_changed(oled_line_t *l, const char *text, uint32_t *since, uint32_t now, bool *changed) {
    oled_line_t next;
    line_set(&next, text);
    if (line_equal(l, &next)) return;
    *l = next;
    if (since) *since = now;
    *changed = true;
}

bool oled_scene_update(oled_scene_t *s, const oled_info_t *info, uint32_t now_ms) {
    bool changed = !s->primed;
    s->primed = true;
    const bool has_meta = info->title[0] || info->artist[0] || info->album[0];
    const char *title, *artist, *album;
    if (info->link == OLED_LINK_IDLE) {
        title = s->name;
        artist = info->pairing ? "Pairing mode..." : "Waiting for phone";
        album = "";
    } else if (!has_meta) {
        title = info->link == OLED_LINK_STREAMING ? "Streaming" : "Connected";
        artist = "No track info";
        album = "";
    } else {
        title = info->title;
        artist = info->artist;
        album = info->album;
    }
    set_if_changed(&s->title, title, &s->since_ms[0], now_ms, &changed);
    set_if_changed(&s->artist, artist, &s->since_ms[1], now_ms, &changed);
    set_if_changed(&s->album, album, &s->since_ms[2], now_ms, &changed);
    oled_line_t left, right;
    build_status(&left, &right, info);
    if (!line_equal(&s->left, &left) || !line_equal(&s->right, &right)) {
        s->left = left;
        s->right = right;
        changed = true;
    }
    return changed;
}

bool oled_scene_animating(const oled_scene_t *s, unsigned pages) {
    return text_px(&s->title, 2) > OLED_WIDTH || text_px(&s->artist, 1) > OLED_WIDTH ||
           (pages >= 8 && text_px(&s->album, 1) > OLED_WIDTH);
}

/* ------------------------------------------------------------------ draw */
/* Column of glyph g: bit r = row r (bit 0 on top). Column 5 is the gap. */
static uint8_t glyph_column(uint8_t g, unsigned col) {
    if (col >= 5 || g >= GLYPH_COUNT) return 0;
    uint8_t bits = 0;
    for (unsigned r = 0; r < 7; ++r)
        if ((font_rows[g][r] >> (4u - col)) & 1u) bits |= (uint8_t)(1u << r);
    return bits;
}

/* 7 font rows -> 14 pixel rows (each doubled), as two page bytes. */
static uint16_t double_rows(uint8_t bits) {
    uint16_t out = 0;
    for (unsigned r = 0; r < 7; ++r)
        if (bits & (1u << r)) out |= (uint16_t)(3u << (2u * r));
    return out;
}

/* Draw one line of glyphs into 'avail' columns starting at x0. If scroll is set the text wraps around
 * (with a gap) and 'offset' is its start position in pixels; otherwise it is clipped. */
static void draw_line(uint8_t *fb, unsigned page, unsigned scale, const oled_line_t *l, unsigned x0, unsigned avail,
                      bool scroll, unsigned offset) {
    const unsigned cell = CELL * scale, total = l->n * cell, cycle = total + GAP_CELLS * cell;
    for (unsigned x = 0; x < avail && x0 + x < OLED_WIDTH; ++x) {
        unsigned v = x + offset;
        if (scroll) v %= cycle;
        if (v >= total) continue;
        uint8_t bits = glyph_column(l->g[v / cell], (v % cell) / scale);
        if (scale == 1) {
            fb[(size_t)page * OLED_WIDTH + x0 + x] = bits;
        } else {
            uint16_t both = double_rows(bits);
            fb[(size_t)page * OLED_WIDTH + x0 + x] = (uint8_t)(both & 0xffu);
            fb[(size_t)(page + 1u) * OLED_WIDTH + x0 + x] = (uint8_t)(both >> 8);
        }
    }
}

static unsigned marquee_offset(unsigned cycle_px, unsigned speed, uint32_t elapsed_ms) {
    uint32_t period = PAUSE_MS + (uint32_t)cycle_px * 1000u / speed;
    uint32_t phase = elapsed_ms % period;
    return phase < PAUSE_MS ? 0u : (unsigned)((phase - PAUSE_MS) * speed / 1000u);
}

/* Centered if it fits, else scrolling across the full width. */
static void draw_text(uint8_t *fb, unsigned page, unsigned scale, const oled_line_t *l, uint32_t since, uint32_t now) {
    unsigned width = text_px(l, scale);
    if (!width) return;
    if (width <= OLED_WIDTH) {
        draw_line(fb, page, scale, l, (OLED_WIDTH - width) / 2u, width, false, 0);
        return;
    }
    unsigned cycle = l->n * CELL * scale + GAP_CELLS * CELL * scale;
    draw_line(fb, page, scale, l, 0, OLED_WIDTH, true, marquee_offset(cycle, scale == 1 ? SPEED_1X : SPEED_2X, now - since));
}

void oled_scene_draw(const oled_scene_t *s, uint8_t *fb, unsigned pages, uint32_t now_ms) {
    if (pages < 4) return;
    memset(fb, 0, (size_t)pages * OLED_WIDTH);
    const unsigned title_page = pages >= 8 ? 2u : 1u, artist_page = pages >= 8 ? 5u : 3u;

    unsigned right_px = text_px(&s->right, 1);
    if (right_px) draw_line(fb, 0, 1, &s->right, OLED_WIDTH - right_px, right_px, false, 0);
    draw_line(fb, 0, 1, &s->left, 0, OLED_WIDTH - (right_px ? right_px + CELL : 0u), false, 0);

    draw_text(fb, title_page, 2, &s->title, s->since_ms[0], now_ms);
    draw_text(fb, artist_page, 1, &s->artist, s->since_ms[1], now_ms);
    if (pages >= 8) draw_text(fb, artist_page + 1u, 1, &s->album, s->since_ms[2], now_ms);
}
