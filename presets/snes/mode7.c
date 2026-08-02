/* SNES Mode 7 demo — rotating checkerboard.
 *
 * Local:  ./scripts/build_snes_local.sh mode7.c mame
 */
#include "snes.h"

static unsigned char mapbuf[128 * 128];
static unsigned char tiles[128];
static unsigned char g_angle;

static void make_tiles(void) {
    unsigned i;
    for (i = 0; i < 64; i++) {
        tiles[i] = 1;
        tiles[64 + i] = 2;
    }
}

static void make_checker_map(void) {
    unsigned i;
    for (i = 0; i < 128 * 128; i++)
        mapbuf[i] = ((i ^ (i >> 7)) & 2) ? 1 : 0;
}

void main(void) {
    snes_brightness(0x80);

    snes_cgram(0, RGB15(4, 4, 12));
    snes_cgram(1, RGB15(28, 8, 8));
    snes_cgram(2, RGB15(8, 24, 28));

    make_tiles();
    make_checker_map();

    snes_mode7_load_map((unsigned)mapbuf, 0, 128 * 128);
    snes_mode7_load_tiles((unsigned)tiles, 0, 128);

    snes_mode7_begin(M7_WRAP);
    snes_mode7_center(512, 512);
    snes_mode7_scroll(0, 0x0180);
    snes_brightness(0x0F);

    g_angle = 0;
    while (1) {
        snes_wait_vblank();
        g_angle += 1;
        snes_mode7_rot(g_angle, 0x0100);
    }
}
