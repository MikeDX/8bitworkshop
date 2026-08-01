/*
 * Midway MCR (91490 / timber) — minimal example.
 * Border tiles + one bouncing HW sprite.
 *
 * Gfx: scripts/gen_mcr_demo_gfx.py → demo_gfx.h
 *   BG pens 0–3 in low half @0x8000; sprites pens 5–7 (91464 banks).
 */
#include "mcr.h"
#include "demo_gfx.h"
#include <string.h>

void main(void);
void mcr_boot(void);

volatile byte mcr_vblank_flag;

void mcr_vblank_isr(void) __naked {
__asm
  push af
  push bc
  push de
  push hl
  push ix
  push iy
  xor  a
  inc  a
  ld   (_mcr_vblank_flag), a
  pop  iy
  pop  ix
  pop  hl
  pop  de
  pop  bc
  pop  af
  ei
  reti
__endasm;
}

void start(void) __naked {
__asm
  .area _HEADER (ABS)
  .org 0x0000
  ld   sp, #0xE800
  di
  jp   _mcr_boot
  .org 0x7F00
  .dw  _mcr_vblank_isr
  .dw  _mcr_vblank_isr
  .dw  _mcr_vblank_isr
  .dw  _mcr_vblank_isr
  .area _CODE
__endasm;
}

void mcr_boot(void) {
  mcr_enable_vblank_irq();
  main();
}

void main(void) {
  byte x, y;
  word sx = 80;
  word sy = 120;
  sbyte dx = 2;
  sbyte dy = 2;

  mcr_init();
  (void)demo_bg_gfx[0];
  (void)demo_spr_gfx[0];

  /* BG palette bank 0 (pens 0–3) */
  set_color(0, 0, 0, 0);
  set_color(1, 0, 0, 7);
  set_color(2, 7, 7, 7);
  set_color(3, 7, 7, 0);
  /* Sprite pal 0 → 91464 bank 48, pens 5–7 (gen bias 4) */
  set_color(48 + 5, 7, 7, 0);
  set_color(48 + 6, 7, 0, 0);
  set_color(48 + 7, 0, 7, 0);

  memset(vram, 0, 0x800);
  for (y = 0; y < TILE_ROWS; y++) {
    for (x = 0; x < TILE_COLS; x++) {
      if (x == 0 || y == 0 || x == TILE_COLS - 1 || y == TILE_ROWS - 1)
        set_tile(x, y, T_SOLID, 0, 0, 0);
      else
        set_tile(x, y, T_BLANK, 0, 0, 0);
    }
  }

  while (1) {
    wait_vblank();
    sx += dx;
    sy += dy;
    if (sx < 8 || sx > 480) dx = (sbyte)-dx;
    if (sy < 8 || sy > 440) dy = (sbyte)-dy;
    set_sprite_xy(0, S_BALL, 0, sx, sy);
  }
}
