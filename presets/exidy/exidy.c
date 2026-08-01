/*
 * Exidy UGB v2 runtime helpers.
 */
#include "exidy.h"

volatile byte video_framecount;

/* VBlank IRQ: ack hardware latch, increment frame counter, RTI. */
void exidy_default_irq(void) {
  asm("lda $5103");
  asm("inc _video_framecount");
  asm("rti");
}

void exidy_init(void) {
  /* Clear screen + character RAM (crt0 only clears Pepper II char area). */
  memset(SCREEN_RAM, 0, 0x400);
  memset(CHAR_RAM, 0, 0x800);
  hide_sprites();
  /* Defaults similar to the classic Venture-ish RGB latch look. */
  set_palette(0b10111100, 0b11111010, 0b01000001);
  video_framecount = 0;
  exidy_set_irq(exidy_default_irq);
}

void exidy_set_irq(void (*handler)(void)) {
  INTVEC = handler;
}

void wait_vblank(void) {
  byte f = video_framecount;
  while (video_framecount == f) { }
}

void clrscr(byte tile) {
  memset(SCREEN_RAM, tile, SCREEN_W * SCREEN_H);
}

void poke_tile(byte x, byte y, byte tile) {
  SCREEN_RAM[y * SCREEN_W + x] = tile;
}

byte peek_tile(byte x, byte y) {
  return SCREEN_RAM[y * SCREEN_W + x];
}

void put_char(byte x, byte y, char ch) {
  poke_tile(x, y, (byte)ch);
}

void put_string(byte x, byte y, const char* s) {
  while (*s) {
    put_char(x++, y, *s++);
  }
}

void set_palette(byte r, byte g, byte b) {
  COLOR_R = r;
  COLOR_G = g;
  COLOR_B = b;
}

/* Soft copy of $5100 — hardware reads return DIP/inputs, not latches. */
static byte sprite_image_latch;

void set_sprite(byte which, byte shape, byte x, byte y) {
  /*
   * Venture/MAME: motion object 1 is shown when bit7 is CLEAR
   * (or bit4 set, or old boards with no collision). Bit5/6 select
   * sprite bank (+16). Motion object 2 is always drawn; its ROM
   * index is (hi nibble) + 32 + 16*bit6.
   */
  byte ctrl = 0x00;
  if (which == 0) {
    SPRITE1_X = x;
    SPRITE1_Y = y;
    sprite_image_latch = (sprite_image_latch & 0xf0) | (shape & 0x0f);
  } else {
    SPRITE2_X = x;
    SPRITE2_Y = y;
    /* shape is the low 4 bits within the MO2 bank (ROM index 32+shape) */
    sprite_image_latch = (sprite_image_latch & 0x0f) | ((shape & 0x0f) << 4);
  }
  SPRITE_IMAGE = sprite_image_latch;
  SPRITE_CTRL = ctrl;
}

/* Place sprite at top-left screen pixel (0,0). Hardware latches are inverted. */
void set_sprite_screen(byte which, byte shape, byte sx, byte sy) {
  set_sprite(which, shape, (byte)(232 - sx), (byte)(240 - sy));
}

void hide_sprites(void) {
  /* Park off-screen (hardware coords are inverted). */
  SPRITE1_X = 0xff;
  SPRITE1_Y = 0xff;
  SPRITE2_X = 0xff;
  SPRITE2_Y = 0xff;
  sprite_image_latch = 0;
  SPRITE_IMAGE = 0;
  SPRITE_CTRL = 0;
}

void load_charset(const byte* tiles, word nbytes) {
  memcpy(CHAR_RAM, tiles, nbytes);
}
