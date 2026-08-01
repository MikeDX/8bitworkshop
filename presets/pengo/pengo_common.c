/*
 * Shared Pengo hardware helpers for 8bitworkshop demos.
 *
 * Owns reset/IRQ CRT at absolute 0x0000 (_HEADER).
 *
 * IRQ mode matches real hardware / MAME pengo.cpp:
 *   - VBLANK is a maskable IRQ
 *   - CPU runs IM1 (RST 38h) — arcade ROM does `im 1` at reset
 *   - MAME removes Pac-Man's I/O port-0 vector latch, so IM2+OUT(0)
 *     does nothing there; homebrew must use IM1 with the ISR at 0x0038
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

        ; IM1: RST 38h — ISR code lives here (not an IM2 vector word)
        .org    0x0038
vblank_isr::
        push    af
        push    bc
        push    de
        push    hl
        push    ix
        push    iy

        ; Ack IRQ (latch bit0 at 0x9040)
        xor     a
        ld      (0x9040), a

        ; Soft sound → hardware Namco WSG @ 0x9000
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

        ; MAME watchdog is 16 VBLANKs — kick every frame (arcade does too)
        xor     a
        ld      (0x9070), a

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

        ; Match arcade / MAME (pengou config): IM1, no I/O vector port
        im      1

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

/*
 * Upright (x,y) → MAME tilemap (col,row) = (y, 27-x), then pacman_scan_rows.
 * Same formula Pengo and Pac-Man use in MAME (pacman_v.cpp).
 */
word vram_addr(byte x, byte y) {
  int col2 = (int)y - 2;
  int row2 = (int)(27 - x) + 2;
  if (col2 & 0x20)
    return (word)(row2 + ((col2 & 0x1f) << 5));
  return (word)(col2 + (row2 << 5));
}

byte* pf_column(byte x) {
  /* Playfield columns are 32 linear bytes at 0x40 + (27-x)*32. */
  return (byte*)(0x8000 + 0x40 + ((word)(27 - x) << 5));
}

void poke_tile(byte x, byte y, byte tile, byte pal) {
  word a;
  if (x >= 28 || y >= 36) return;
  a = vram_addr(x, y);
  *((byte*)(0x8000 + a)) = tile;
  *((byte*)(0x8400 + a)) = pal;
}

void poke_pal(byte x, byte y, byte pal) {
  if (x >= 28 || y >= 36) return;
  *((byte*)(0x8400 + vram_addr(x, y))) = pal;
}

byte peek_tile(byte x, byte y) {
  if (x >= 28 || y >= 36) return T_BLANK;
  return *((byte*)(0x8000 + vram_addr(x, y)));
}

void fill_column(byte x, byte y0, byte n, byte tile, byte pal) {
  byte* vt;
  byte* ct;
  byte i;
  if (x >= 28 || y0 < 2 || y0 >= 34) return;
  if ((word)y0 + n > 34) n = (byte)(34 - y0);
  vt = pf_column(x) + (y0 - 2);
  ct = vt + 0x400; /* color RAM is +0x400 from video RAM */
  for (i = 0; i < n; i++) {
    vt[i] = tile;
    ct[i] = pal;
  }
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

void set_gfx_bank(byte bank) { gfx_bank = bank & 1; }
void set_palette_bank(byte bank) { palette_bank = bank & 1; }
void set_colortable_bank(byte bank) { colortable_bank = bank & 1; }
void set_flip_screen(byte on) { flip_screen = on & 1; }

void scroll_column_y(byte x, sbyte dy, byte fill_tile, byte fill_pal) {
  byte* vt;
  byte* ct;
  byte i;
  byte n = 32; /* playfield rows y=2..33 */
  if (x >= 28 || dy == 0) return;
  vt = pf_column(x);
  ct = vt + 0x400;
  if (dy > 0) {
    byte d = (byte)dy;
    if (d >= n) {
      for (i = 0; i < n; i++) { vt[i] = fill_tile; ct[i] = fill_pal; }
      return;
    }
    i = n;
    while (i > d) {
      i--;
      vt[i] = vt[i - d];
      ct[i] = ct[i - d];
    }
    for (i = 0; i < d; i++) {
      vt[i] = fill_tile;
      ct[i] = fill_pal;
    }
  } else {
    byte ady = (byte)(-dy);
    if (ady >= n) {
      for (i = 0; i < n; i++) { vt[i] = fill_tile; ct[i] = fill_pal; }
      return;
    }
    for (i = 0; i < n - ady; i++) {
      vt[i] = vt[i + ady];
      ct[i] = ct[i + ady];
    }
    for (i = n - ady; i < n; i++) {
      vt[i] = fill_tile;
      ct[i] = fill_pal;
    }
  }
}

void scroll_playfield_y(sbyte dy, byte fill_tile, byte fill_pal) {
  byte x;
  for (x = 0; x < 28; x++)
    scroll_column_y(x, dy, fill_tile, fill_pal);
}

