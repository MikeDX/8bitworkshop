/*
 * Pocket Gal hardware helpers for the Pac-Man port.
 * Same API surface as presets/pacman/pacman_common.h (pacmanonpacman).
 */
#ifndef PACMAN_COMMON_H
#define PACMAN_COMMON_H

typedef unsigned char byte;
typedef unsigned short word;
typedef signed char sbyte;

void main(void);

/* Stubs kept for source compatibility */
#define interrupt_enable (*(volatile byte*)0x0000)
#define sound_enable     _pac_sound_enable
#define flip_screen      _pac_flip_screen
#define watchdog         _pac_watchdog

extern volatile byte _pac_sound_enable;
extern volatile byte _pac_flip_screen;
extern volatile byte _pac_watchdog;

extern volatile byte video_framecount;
extern void (*pac_vblank_hook)(void);
void pac_run_vblank_hook(void);
void pac_irq_enable(void);

/* P1 @ $1800 active-low. With 90° CW framebuffer + MAME -rol, stick
 * directions match the upright portrait view. */
#define UP1    (!(_pac_p1 & 0x08))
#define LEFT1  (!(_pac_p1 & 0x02))
#define RIGHT1 (!(_pac_p1 & 0x01))
#define DOWN1  (!(_pac_p1 & 0x04))
#define FIRE1  (!(_pac_p1 & 0x80))
#define COIN1  (!(_pac_p2 & 0x10))
#define START1 (!(_pac_p1 & 0x10))

extern volatile byte _pac_p1;
extern volatile byte _pac_p2;

#define T_BLANK   0x40
#define T_DOT     0x10
#define T_POWER   0x14

word vram_addr(byte x, byte y);
void poke_tile(byte x, byte y, byte tile, byte pal);
void poke_pal(byte x, byte y, byte pal);
byte peek_tile(byte x, byte y);
void clrscr(byte pal);
void wait_vblank(void);
void put_digit(byte x, byte y, byte d, byte pal);
void put_char(byte x, byte y, char ch, byte pal);
void put_string(byte x, byte y, const char* s, byte pal);
void set_sprite(byte i, byte shape, byte color, byte sx, byte sy);
/* flags: bit0 = flipY, bit1 = flipX (Pac-Man convention) */
void set_sprite_ex(byte i, byte shape, byte color, byte sx, byte sy, byte flags);
void hide_sprite(byte i);
void hide_all_sprites(void);

void sound_voice(byte voice, word freq, byte vol, byte wave);
void sound_vol(byte voice, byte vol);
void sound_off(void);
void sound_beep(word freq, byte frames);

void pacman_hw_init(void);

#endif
