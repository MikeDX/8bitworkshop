/*
 * Shared Pengo hardware helpers for 8bitworkshop demos.
 *
 * Link helpers only:
 *   //#link "pengo_common.c"
 *
 * CRT0 (reset + IM2 VBLANK IRQ) lives in pengo_common — do not define start().
 * Optional per-frame work: set pengo_vblank_hook, then pengo_irq_enable().
 *
 * Joystick bits differ from Pac-Man: UP, DOWN, LEFT, RIGHT (MAME pengo IN0).
 *
 * VRAM: same pacman_scan_rows map as Pac-Man (disjointed). See vram_addr().
 */
#ifndef PENGO_COMMON_H
#define PENGO_COMMON_H

typedef unsigned char byte;
typedef unsigned short word;
typedef signed char sbyte;

void main(void);

/* memory-mapped I/O (MAME pengo_map) */
#define interrupt_enable (*(volatile byte*)0x9040)
#define sound_enable     (*(volatile byte*)0x9041)
#define palette_bank     (*(volatile byte*)0x9042)
#define flip_screen      (*(volatile byte*)0x9043)
#define colortable_bank  (*(volatile byte*)0x9046)
#define gfx_bank         (*(volatile byte*)0x9047)
#define watchdog         (*(volatile byte*)0x9070)
#define input0           (*(volatile byte*)0x90c0)
#define input1           (*(volatile byte*)0x9080)

extern volatile byte video_framecount;

extern void (*pengo_vblank_hook)(void);
void pengo_run_vblank_hook(void);
void pengo_irq_enable(void);

#define UP1    (!(input0 & 0x01))
#define DOWN1  (!(input0 & 0x02))
#define LEFT1  (!(input0 & 0x04))
#define RIGHT1 (!(input0 & 0x08))
#define COIN1  (!(input0 & 0x10))
#define FIRE1  (!(input0 & 0x80))
#define START1 (!(input1 & 0x20))

#define T_BLANK   0x40
#define T_DOT     0x10
#define T_POWER   0x14
#define T_CIRCLE  'O'

#define S_PLAYER  0
#define S_ALIEN   1

#define PAL_YELLOW 9
#define PAL_CYAN   5
#define PAL_PINK   3
#define PAL_ORANGE 7
#define PAL_BLUE   5
#define PAL_RED    1
#define PAL_WHITE  15

/*
 * Tile coords are upright/cabinet: x=0..27 left→right, y=0..35 top→bottom.
 *
 * Pengo shares Pac-Man's pacman_scan_rows VRAM map (MAME). It is NOT a linear
 * 28×36 grid: top/bottom status rows and the playfield use different strides.
 *
 * Cheap axis: along upright Y (MAME col) inside the playfield — consecutive
 * addresses. Use pf_column()/fill_column() for vertical strips. Horizontal
 * runs (put_string) jump −32 per tile; that cost is hardware, not fixable.
 */
word vram_addr(byte x, byte y);
/* Playfield only (x=0..27, y=2..33): pointer to 32 linear tiles in that column. */
byte* pf_column(byte x);
void poke_tile(byte x, byte y, byte tile, byte pal);
void poke_pal(byte x, byte y, byte pal);
byte peek_tile(byte x, byte y);
void fill_column(byte x, byte y0, byte n, byte tile, byte pal);
void clrscr(byte pal);
void wait_vblank(void);
void put_digit(byte x, byte y, byte d, byte pal);
void put_char(byte x, byte y, char ch, byte pal);
void put_string(byte x, byte y, const char* s, byte pal);
void set_sprite(byte i, byte shape, byte color, byte sx, byte sy);
void set_sprite_ex(byte i, byte shape, byte color, byte sx, byte sy, byte flags);
void hide_sprite(byte i);
void hide_all_sprites(void);

void sound_voice(byte voice, word freq, byte vol, byte wave);
void sound_vol(byte voice, byte vol);
void sound_off(void);

#endif
