/*
 * Shared Pengo hardware helpers for 8bitworkshop demos.
 *
 * Owns reset/IRQ CRT at absolute 0x0000 (_HEADER). Pengo VBLANK is a
 * maskable IRQ (IM2), same family as Pac-Man / MAME.
 */
#pragma opt_code_speed
#include "pengo_common.h"

/*
 * Absolute CRT0 @ 0x0000 (ABS _HEADER, through ~0xC9). Game _CODE starts
 * at 0xCA (platforms.ts codeseg_start).
 */
static void pengo_crt0(void) __naked {
__asm
        .area   _HEADER (ABS)
        .org    0x0000
_start::
        jp      real_start

        .org    0x0038
        .dw     vblank_isr

vblank_isr:
        push    af
        push    bc
        push    de
        push    hl
        push    ix
        push    iy

        ; Ack IRQ (latch bit0 at 0x9040)
        xor     a
        ld      (0x9040), a

        ; Soft sound → hardware WSG @ 0x9000
        ld      hl, #0x8e8c
        ld      de, #0x9010
        ld      bc, #0x0010
        ldir

        ld      a, (0x8ecc)
        and     a
        ld      a, (0x8ecf)
        jr      nz, 00010$
        ld      a, (0x8e9f)
00010$:
        ld      (0x9005), a
        ld      a, (0x8edc)
        and     a
        ld      a, (0x8edf)
        jr      nz, 00011$
        ld      a, (0x8eaf)
00011$:
        ld      (0x900a), a
        ld      a, (0x8eec)
        and     a
        ld      a, (0x8eef)
        jr      nz, 00012$
        ld      a, (0x8ebf)
00012$:
        ld      (0x900f), a

        ld      a, (_video_framecount)
        inc     a
        ld      (_video_framecount), a

        call    _pengo_run_vblank_hook

        ld      a, #1
        ld      (0x9040), a

        pop     iy
        pop     ix
        pop     hl
        pop     de
        pop     bc
        pop     af
        ei
        reti

real_start:
        ld      sp, #0x8fc0
        ld      bc, #l__INITIALIZER
        ld      a, b
        or      a, c
        jr      z, 00001$
        ld      de, #s__INITIALIZED
        ld      hl, #s__INITIALIZER
        ldir
00001$:
        ld      hl, #0x8e8c
        ld      de, #0x8e8d
        ld      (hl), #0
        ld      bc, #0x006f
        ldir

        xor     a
        ld      (_pengo_vblank_hook+0), a
        ld      (_pengo_vblank_hook+1), a

        xor     a
        ld      i, a
        im      2
        ld      a, #0x38
        out     (0), a

        jp      _main
        .area   _CODE
__endasm;
}

volatile byte video_framecount;
void (*pengo_vblank_hook)(void);

void pengo_run_vblank_hook(void) {
  if (pengo_vblank_hook)
    pengo_vblank_hook();
}

void pengo_irq_enable(void) {
  interrupt_enable = 1;
  __asm__("ei");
}

word vram_addr(byte x, byte y) {
  if (y < 2)
    return 0x3c0 + (word)y * 32 + (29 - x);
  if (y >= 34)
    return (word)(y - 34) * 32 + (29 - x);
  return 0x40 + (word)(27 - x) * 32 + (y - 2);
}

void poke_tile(byte x, byte y, byte tile, byte pal) {
  word a;
  if (x >= 28 || y >= 36) return;
  a = vram_addr(x, y);
  *((byte*)(0x8000 + a)) = tile;
  *((byte*)(0x8400 + a)) = pal;
}

byte peek_tile(byte x, byte y) {
  if (x >= 28 || y >= 36) return T_BLANK;
  return *((byte*)(0x8000 + vram_addr(x, y)));
}

void clrscr(byte pal) {
  word i;
  for (i = 0; i < 0x400; i++) {
    ((byte*)0x8000)[i] = T_BLANK;
    ((byte*)0x8400)[i] = pal;
    if ((i & 63) == 0) watchdog = 0;
  }
}

void wait_vblank(void) {
  byte f = video_framecount;
  while (video_framecount == f) watchdog = 0;
}

void put_digit(byte x, byte y, byte d, byte pal) {
  if (d > 9) d = 9;
  poke_tile(x, y, (byte)('0' + d), pal);
}

void put_char(byte x, byte y, char ch, byte pal) {
  byte t;
  if (ch == ' ' || ch == '\t') t = T_BLANK;
  else if (ch >= 'a' && ch <= 'z') t = (byte)(ch - 'a' + 'A');
  else t = (byte)ch;
  poke_tile(x, y, t, pal);
}

void put_string(byte x, byte y, const char* s, byte pal) {
  while (*s) {
    put_char(x++, y, *s++, pal);
    if (x >= 28) break;
  }
}

/* Sprite attrs @ 0x8FF0, coords @ 0x9020 (bottom-right origin, same as Pac-Man) */
void set_sprite_ex(byte i, byte shape, byte color, byte sx, byte sy, byte flags) {
  byte* attr = (byte*)(0x8ff0 + (i << 1));
  byte* pos = (byte*)(0x9020 + (i << 1));
  attr[0] = (byte)((shape << 2) | (flags & 3));
  attr[1] = color;
  pos[0] = (byte)(239 - sx);
  pos[1] = (byte)(272 - sy);
}

void set_sprite(byte i, byte shape, byte color, byte sx, byte sy) {
  set_sprite_ex(i, shape, color, sx, sy, 0);
}

void hide_sprite(byte i) {
  set_sprite(i, 0, 0, 0, 0);
  ((byte*)0x9020)[i * 2] = 0;
  ((byte*)0x9020)[i * 2 + 1] = 0;
}

void hide_all_sprites(void) {
  byte i;
  for (i = 0; i < 8; i++) hide_sprite(i);
}

void sound_voice(byte voice, word freq, byte vol, byte wave) {
  word wave_addr, freq_addr;
  byte i, nibbles;
  if (voice > 2) return;
  if (voice == 0) { wave_addr = 0x9005; freq_addr = 0x9010; nibbles = 5; }
  else if (voice == 1) { wave_addr = 0x900a; freq_addr = 0x9016; nibbles = 4; }
  else { wave_addr = 0x900f; freq_addr = 0x901b; nibbles = 4; }
  if (voice != 0) freq >>= 4;
  *(volatile byte*)wave_addr = wave & 7;
  for (i = 0; i < nibbles; i++) {
    ((volatile byte*)freq_addr)[i] = freq & 0x0f;
    freq >>= 4;
  }
  ((volatile byte*)freq_addr)[nibbles] = vol & 0x0f;
}

void sound_vol(byte voice, byte vol) {
  if (voice == 0) ((volatile byte*)0x9015)[0] = vol & 0x0f;
  else if (voice == 1) ((volatile byte*)0x901a)[0] = vol & 0x0f;
  else if (voice == 2) ((volatile byte*)0x901f)[0] = vol & 0x0f;
}

void sound_off(void) {
  sound_voice(0, 0, 0, 0);
  sound_voice(1, 0, 0, 0);
  sound_voice(2, 0, 0, 0);
}
