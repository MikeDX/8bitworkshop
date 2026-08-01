/*
 * Exidy Universal Game Board (UGB) v2 helpers for 8bitworkshop.
 *
 * Hardware (MAME exidy.cpp):
 *   6502 @ ~705 kHz, 256×256 tiles + 2×16×16 sprites
 *   Screen RAM $4000, char RAM $4800, color latches $5210-$5212
 *   Inputs $5101 (active low), VBlank IRQ via CRT0 INTVEC
 *
 * Include from presets:
 *   #include "exidy.h"
 */
#ifndef EXIDY_H
#define EXIDY_H

#include <peekpoke.h>
#include <string.h>

typedef unsigned char byte;
typedef unsigned int word;

/* Hardware */
#define SCREEN_RAM   ((byte*)0x4000)
#define CHAR_RAM     ((byte*)0x4800)
#define SPRITE1_X    (*(volatile byte*)0x5000)
#define SPRITE1_Y    (*(volatile byte*)0x5040)
#define SPRITE2_X    (*(volatile byte*)0x5080)
#define SPRITE2_Y    (*(volatile byte*)0x50C0)
#define SPRITE_IMAGE (*(volatile byte*)0x5100)  /* lo nibble spr1, hi spr2 */
#define SPRITE_CTRL  (*(volatile byte*)0x5101)  /* enable / set select */
#define INPUTS       (*(volatile byte*)0x5101)  /* read: controls (active low) */
#define INT_LATCH    (*(volatile byte*)0x5103)
#define COLOR_R      (*(volatile byte*)0x5210)
#define COLOR_G      (*(volatile byte*)0x5211)
#define COLOR_B      (*(volatile byte*)0x5212)

/* Inputs (active low — macros read as pressed=true) */
#define START1  (!(INPUTS & 0x01))
#define START2  (!(INPUTS & 0x02))
#define RIGHT1  (!(INPUTS & 0x04))
#define LEFT1   (!(INPUTS & 0x08))
#define FIRE1   (!(INPUTS & 0x10))
#define UP1     (!(INPUTS & 0x20))
#define DOWN1   (!(INPUTS & 0x40))
#define COIN1   (!(INPUTS & 0x80))

/* Screen is 32×32 tiles */
#define SCREEN_W 32
#define SCREEN_H 32

/* CRT0 exports a ZP interrupt vector */
extern void (*INTVEC)(void);
#pragma zpsym("INTVEC")

/* Shared frame counter — demos may also keep their own */
extern volatile byte video_framecount;

void exidy_init(void);
void exidy_default_irq(void);
void exidy_set_irq(void (*handler)(void));
void wait_vblank(void);
void clrscr(byte tile);
void poke_tile(byte x, byte y, byte tile);
byte peek_tile(byte x, byte y);
void put_char(byte x, byte y, char ch);
void put_string(byte x, byte y, const char* s);
void set_palette(byte r, byte g, byte b);
void set_sprite(byte which, byte shape, byte x, byte y);
void set_sprite_screen(byte which, byte shape, byte sx, byte sy);
void hide_sprites(void);
void load_charset(const byte* tiles, word nbytes);

#endif
