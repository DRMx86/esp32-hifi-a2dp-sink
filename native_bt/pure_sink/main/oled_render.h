#ifndef PURE_SINK_OLED_RENDER_H
#define PURE_SINK_OLED_RENDER_H
/* Text screens for a 128x32 / 128x64 monochrome page-addressed OLED (SSD1306 layout: one byte = 8 vertical
 * pixels, bit 0 on top). Pure C, no ESP-IDF or Bluetooth dependency, so it also builds on a PC
 * (see ../host_test/oled_render_test.c). 5x7 font in 6x8 cells, ASCII plus a few folded Latin-1 and
 * punctuation characters; anything else shows as '?'.
 *
 * Layout (64 rows): status bar | title 2x (scrolls) | artist | album. 32 rows: status | title 2x | artist.
 * Text wider than the display scrolls left with a pause at the start of every pass. */
#include <stdbool.h>
#include <stdint.h>

#define OLED_WIDTH 128u
#define OLED_TEXT_BYTES 96u /* same as SINK_AVRCP_TEXT_BYTES: title/artist/album arrive as UTF-8 */
#define OLED_CODEC_BYTES 16u
#define OLED_NAME_CHARS 24u

enum { OLED_LINK_IDLE = 0, OLED_LINK_CONNECTED, OLED_LINK_STREAMING };
#define OLED_PLAY_UNKNOWN 0xffu /* AVRCP play status not known (no AVRCP link yet) */

/* Everything the screen shows. Filled by the Bluetooth host owner, copied to the display task. */
typedef struct {
    uint8_t link;         /* OLED_LINK_* */
    uint8_t play_status;  /* AVRCP: 0 stopped, 1 playing, 2 paused, 3/4 seek, 0xff unknown */
    uint8_t volume;       /* AVRCP absolute volume 0..127 */
    uint8_t sample_bytes; /* PCM bytes per sample of the running stream (2/3/4), 0 if none */
    bool pairing;         /* pairing window open */
    uint32_t rate;        /* Hz of the running stream, 0 if none */
    char codec[OLED_CODEC_BYTES];
    char title[OLED_TEXT_BYTES], artist[OLED_TEXT_BYTES], album[OLED_TEXT_BYTES];
} oled_info_t;

typedef struct {
    uint8_t g[OLED_TEXT_BYTES]; /* font glyph indices */
    uint8_t n;
} oled_line_t;

typedef struct {
    oled_line_t title, artist, album, left, right; /* what is on screen; compared to detect changes */
    uint32_t since_ms[3];   /* marquee clocks: title, artist, album */
    char name[OLED_NAME_CHARS + 1];
    bool primed;
} oled_scene_t;

void oled_scene_init(oled_scene_t *scene, const char *device_name);
/* Apply new info. Returns true if anything visible changed (text restarts its scroll from the left). */
bool oled_scene_update(oled_scene_t *scene, const oled_info_t *info, uint32_t now_ms);
/* True while some visible line is wider than the display and therefore needs periodic redraws. */
bool oled_scene_animating(const oled_scene_t *scene, unsigned pages);
/* Clear fb (pages * OLED_WIDTH bytes) and draw the scene. pages is 4 (128x32) or 8 (128x64). */
void oled_scene_draw(const oled_scene_t *scene, uint8_t *fb, unsigned pages, uint32_t now_ms);
#endif
