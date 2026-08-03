/*
 * Pocket Gal (BAC06) implementation of Pac-Man hardware helpers.
 *
 * Pac-Man is a vertical game (28×36 tiles). Pocket Gal is landscape (32×28).
 * We rotate 90° CW, then shift so the playfield fits the visible raster:
 *   +2 portrait X (right / landscape down) → hy band for 16px top crop
 *   +2 portrait Y (up   / landscape right) → show logical rows 2..33
 *     (row 2 = compact HUD; 3..33 = full maze; lives/credit elsewhere)
 *   logical (lx, ly) → hardware (31-(ly-2), lx+2)   [tiles]
 *   logical (px, py) → hardware (240-py+16, px+16)  [sprite top-left]
 * Gfx in pacman_gfx.c are pre-rotated 90° CW to match.
 *
 * View upright in MAME with:  mame pcktgal2 -rol
 * (or rotate the monitor 90° CCW in a cabinet)
 */
#include <peekpoke.h>
#include "pacman_common.h"

volatile byte video_framecount;
volatile byte _pac_sound_enable;
volatile byte _pac_flip_screen;
volatile byte _pac_watchdog;
volatile byte _pac_p1 = 0xff;
volatile byte _pac_p2 = 0xff;
void (*pac_vblank_hook)(void);

extern void (*INTVEC)(void);
#pragma zpsym("INTVEC")

static void nmi_handler(void) {
  asm("inc _video_framecount");
  asm("rti");
}

void pac_run_vblank_hook(void) {
  if (pac_vblank_hook) pac_vblank_hook();
}

void pac_irq_enable(void) {
  INTVEC = nmi_handler;
}

void pacman_hw_init(void) {
  byte i;
  INTVEC = nmi_handler;
  POKE(0x1800, 0x03); /* 8x8, no forced TILE_FLIPX */
  POKE(0x1810, 0); POKE(0x1811, 0);
  POKE(0x1812, 0); POKE(0x1813, 0);
  for (i = 0; i < 64; i++)
    POKE(0x1000 + i * 4, 0xf8);
  _pac_p1 = 0xff;
  _pac_p2 = 0xff;
}

static void poll_inputs(void) {
  _pac_p1 = PEEK(0x1800);
  _pac_p2 = PEEK(0x1a00);
}

/* ---- 90° CW logical (portrait) → hardware (landscape) ---- */

static word hw_vram_addr(byte hx, byte hy) {
  return (word)(0x0800 + ((word)hy * 32u + hx) * 2u);
}

/*
 * Portrait +2 X ≡ landscape +2 down (hy) — matches 16px top crop (hy 2..29).
 * Portrait +2 Y ≡ landscape +2 right (hx) — viewport shows logical ty 2..33.
 */
#define LOGIC_X_SHIFT  2
#define LOGIC_Y_SHIFT  2

/* Tile: (tx,ty) → (31-(ty-2), tx+2). Visible ty is 2..33. */
static byte logic_tile_to_hw(byte tx, byte ty, byte* hx, byte* hy) {
  if (tx >= 28) return 0;
  if (ty < LOGIC_Y_SHIFT || ty >= (byte)(LOGIC_Y_SHIFT + 32)) return 0;
  *hx = (byte)(31 - (ty - LOGIC_Y_SHIFT));
  *hy = (byte)(tx + LOGIC_X_SHIFT);
  return 1;
}

word vram_addr(byte x, byte y) {
  byte hx, hy;
  if (!logic_tile_to_hw(x, y, &hx, &hy)) return 0x0800;
  return hw_vram_addr(hx, hy);
}

void poke_tile(byte x, byte y, byte tile, byte pal) {
  byte hx, hy;
  word addr;
  word w;
  if (!logic_tile_to_hw(x, y, &hx, &hy)) return;
  addr = hw_vram_addr(hx, hy);
  w = ((word)(pal & 0x0f) << 12) | tile;
  POKE(addr, w >> 8);
  POKE(addr + 1, w & 0xff);
}

void poke_pal(byte x, byte y, byte pal) {
  byte hx, hy;
  word addr;
  word old;
  if (!logic_tile_to_hw(x, y, &hx, &hy)) return;
  addr = hw_vram_addr(hx, hy);
  old = ((word)PEEK(addr) << 8) | PEEK(addr + 1);
  poke_tile(x, y, (byte)(old & 0xff), pal);
}

byte peek_tile(byte x, byte y) {
  byte hx, hy;
  word addr;
  if (!logic_tile_to_hw(x, y, &hx, &hy)) return 0x40;
  addr = hw_vram_addr(hx, hy);
  return PEEK(addr + 1);
}

void clrscr(byte pal) {
  /* Clear full 32×32 map window (hy 28–29 hold shifted maze right edge). */
  byte hx, hy;
  word w = ((word)(pal & 0x0f) << 12) | 0x40;
  for (hy = 0; hy < 32; hy++) {
    for (hx = 0; hx < 32; hx++) {
      word addr = hw_vram_addr(hx, hy);
      POKE(addr, w >> 8);
      POKE(addr + 1, w & 0xff);
    }
  }
}

void wait_vblank(void) {
  byte f;
  poll_inputs();
  f = video_framecount;
  while (f == video_framecount) { }
  poll_inputs();
  pac_run_vblank_hook();
}

void put_digit(byte x, byte y, byte d, byte pal) {
  put_char(x, y, (char)('0' + (d % 10)), pal);
}

void put_char(byte x, byte y, char ch, byte pal) {
  byte t;
  if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
  if (ch >= '0' && ch <= '9') t = (byte)ch;
  else if (ch >= 'A' && ch <= 'Z') t = (byte)ch;
  else if (ch == ' ') t = 0x40;
  else if (ch == '/') t = 0x3A;
  else if (ch == '-') t = 0x3B;
  else if (ch == '!') t = 0x5B;
  else if (ch == '"') t = 0x26;
  else t = (byte)ch;
  poke_tile(x, y, t, pal);
}

void put_string(byte x, byte y, const char* s, byte pal) {
  while (*s) put_char(x++, y, *s++, pal);
}

static byte spr_color_bank(byte pal) {
  if (pal == 9) return 0;
  if (pal == 0x11) return 6;
  if (pal == 0x12) return 4;
  if (pal == 0x14) return 2;
  if (pal == 0x18) return 0;
  if (pal == 0x19) return 4;
  return (byte)(pal & 7);
}

void set_sprite_ex(byte i, byte shape, byte color, byte sx, byte sy, byte flags) {
  unsigned int a;
  byte flip;
  byte bank;
  byte hx, hy;
  if (i >= 64) return;

  /*
   * Logical sprite top-left (sx,sy) in portrait pixels.
   * 90° CW + shifts: (sx,sy) → (240-sy+16, sx+16) for 16×16.
   * sy < 16 is above the viewport (logical y 2..33).
   */
  if (sy < (byte)(LOGIC_Y_SHIFT * 8)) {
    hide_sprite(i);
    return;
  }
  hx = (byte)(240 - sy + (LOGIC_Y_SHIFT * 8));
  hy = (byte)(sx + (LOGIC_X_SHIFT * 8));

  a = 0x1000 + (unsigned int)i * 4u;
  bank = spr_color_bank(color);
  /*
   * Pac flags: bit0=flipY, bit1=flipX.
   * After 90° CW of the sprite art, logical flipX ↔ hardware flipY, etc.
   * DECO: bit1=flipy, bit2=flipx.
   */
  flip = 0;
  if (flags & 2) flip |= 0x02; /* logical flipX → hw flipy */
  if (flags & 1) flip |= 0x04; /* logical flipY → hw flipx */

  POKE(a + 0, (byte)(240 - hy));
  POKE(a + 1, (byte)((bank << 4) | flip | ((shape >> 8) & 1)));
  POKE(a + 2, (byte)(240 - hx));
  POKE(a + 3, (byte)(shape & 0xff));
}

void set_sprite(byte i, byte shape, byte color, byte sx, byte sy) {
  set_sprite_ex(i, shape, color, sx, sy, 0);
}

void hide_sprite(byte i) {
  if (i >= 64) return;
  POKE(0x1000 + (unsigned int)i * 4u, 0xf8);
}

void hide_all_sprites(void) {
  byte i;
  for (i = 0; i < 64; i++) hide_sprite(i);
}

void sound_voice(byte voice, word freq, byte vol, byte wave) {
  (void)voice; (void)freq; (void)vol; (void)wave;
}
void sound_vol(byte voice, byte vol) { (void)voice; (void)vol; }
void sound_off(void) {}
void sound_beep(word freq, byte frames) { (void)freq; (void)frames; }
