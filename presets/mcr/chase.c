/*
 * Chase for Midway MCR (91490 / timber) — port of Shiru's NES Chase.
 *
 * NES cells are 16×16; MCR shows 16×16 tiles (8×8×2) and 32×32 sprites.
 * Each map cell = 2×2 MCR tiles (32×32) so cell size matches sprites and
 * relative scale matches NES on the 512-wide canvas.
 *
 * Graphics: scripts/gen_mcr_chase_gfx.py → chase_gfx.h
 * Levels: extracted from presets/nes/chase/level*_nam.h
 */
#include <string.h>
#include "mcr.h"
#include "chase_gfx.h"

void main(void);
void mcr_boot(void);

volatile byte mcr_vblank_flag;

/* Naked ISR for CTC (IM2). Must preserve all regs — can fire anytime. */
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

/*
 * Absolute header: reset @0, IM2 vector table @0x1F00.
 * .area _CODE restored so subsequent C lands in the program bank.
 */
void start(void) __naked {
__asm
  .area _HEADER (ABS)
  .org 0x0000
  ld   sp, #0xE800
  di
  jp   _mcr_boot
  /* IM2 table: CTC vectors are (base&0xF8)|(ch<<1) — fill CH0..CH3 */
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

/* NES map geometry */
#define MAP_W        16
#define MAP_H        13
#define MAP_Y0       2   /* NES tile-rows above map[0] (HUD / pad) */
#define LEVELS_ALL   5
#define ACTORS_MAX   4

#define TILE_SIZE    16  /* NES logical pixels per cell */
#define TILE_SIZE_BIT 4
#define FP_BITS      4
#define TILE_TO_POS(t)  ((word)(t) << (TILE_SIZE_BIT + FP_BITS))
#define POS_TO_TILE(p)  ((byte)((p) >> (TILE_SIZE_BIT + FP_BITS)))

#define T_BLANK  0  /* outside maze (NES nametable 0x00) */
#define T_FLOOR  1  /* walkable empty (NES 0x44) */
#define T_WALL   2
#define T_ITEM   3

/* Match NES pad bits used as directions */
#define DIR_NONE  0
#define DIR_LEFT  1
#define DIR_RIGHT 2
#define DIR_UP    4
#define DIR_DOWN  8

byte joy_left, joy_right, joy_up, joy_down, joy_fire;
byte fire_prev;

/* ---- Minimal AY SFX (note, vol, frames; 0xff = end) ----
 * Edited in Asset Editor via {sfx:"ay"} tags on each table. */
#define NOTE_LO 0x10
#define AY_ENABLE 7
#define AY_VOL_A  8

const word ay_period[48] = {
  1721, 1625, 1535, 1448, 1367, 1290, 1218, 1149,
  1085, 1024,  967,  912,  861,  813,  767,  724,
   683,  645,  609,  575,  542,  512,  483,  456,
   431,  406,  383,  362,  342,  322,  304,  287,
   271,  256,  242,  228,  215,  203,  192,  181,
   171,  161,  152,  144,  136,  128,  121,  114,
};

const byte sfx_start[] = /*{sfx:"ay",stride:3}*/ {
  0x28,15,4, 0x2d,14,4, 0x2f,14,4, 0x34,13,4, 0x39,12,4, 0x3b,12,4,
  0x28,12,4, 0x2d,11,4, 0x2f,11,4, 0x34,10,4, 0x39,10,4, 0x3b,9,4,
  0x28,9,4,  0x2d,8,4,  0x2f,8,4,  0x34,7,4,  0x39,7,4,  0x3b,6,4,
  0xff
};
const byte sfx_item[] = /*{sfx:"ay",stride:3}*/ {
  0x28,12,1, 0x2d,11,1, 0x2c,10,1, 0x2f,9,1,
  0x28,8,1,  0x2d,7,1,  0x2c,6,1,  0x2f,5,1,
  0xff
};
const byte sfx_hit[] = /*{sfx:"ay",stride:3}*/ {
  0x18,12,2, 0x14,10,2, 0x10,8,3, 0x0c,6,4, 0x08,4,6,
  0xff
};

static const byte* sfx_ptr;
byte sfx_timer;

void sound_init(void) {
  mcr_ay1(AY_ENABLE, 0xfe); /* tone A on; noise/B/C off */
  mcr_ay1(AY_VOL_A, 0);
  mcr_ay2(AY_ENABLE, 0xff);
  sfx_ptr = 0;
  sfx_timer = 0;
}

void sfx_stop(void) {
  sfx_ptr = 0;
  sfx_timer = 0;
  mcr_ay1(AY_VOL_A, 0);
}

void sfx_play(const byte* seq) {
  sfx_ptr = seq;
  sfx_timer = 0;
}

void sfx_update(void) {
  byte note, vol, frames;
  if (!sfx_ptr) return;
  if (sfx_timer) {
    sfx_timer--;
    if (sfx_timer) return;
  }
  note = *sfx_ptr++;
  if (note == 0xff) {
    sfx_stop();
    return;
  }
  vol = *sfx_ptr++;
  frames = *sfx_ptr++;
  if (!frames) frames = 1;
  sfx_timer = frames;
  if (note < NOTE_LO) {
    mcr_ay1(AY_VOL_A, 0);
  } else {
    word p = ay_period[note - NOTE_LO];
    mcr_ay1(0, (byte)p);
    mcr_ay1(1, (byte)(p >> 8));
    mcr_ay1(AY_VOL_A, (byte)(vol & 15));
  }
}

void read_controls(void) {
  joy_left  = LEFT1;
  joy_right = RIGHT1;
  joy_up    = UP1;
  joy_down  = DOWN1;
  joy_fire  = FIRE1 || START1;
}

typedef struct {
  word x, y, cnt, speed;
  byte dir, kind, wait;
} Actor;

/* NES levels — 16×13, from nametables (even NT cols, every other NT row). */
const byte level_h[LEVELS_ALL] = { 13, 13, 13, 13, 13 };

const char* const levels[LEVELS_ALL][MAP_H] = {
  {
  "                ",
  "                ",
  "    ########    ",
  "    #P*****#    ",
  "    #*####*#    ",
  "    #******#    ",
  "    #*####*#    ",
  "    #*****1#    ",
  "    ########    ",
  "                ",
  "                ",
  "                ",
  "                ",
  },
  {
  "                ",
  "   ##########   ",
  "   #P***#**1#   ",
  "   #*##*#*#*#   ",
  "   #********#   ",
  "   ###*#*#*##   ",
  "   #********#   ",
  "   #*#*#*##*#   ",
  "   #2*******#   ",
  "   ##########   ",
  "                ",
  "                ",
  "                ",
  },
  {
  "                ",
  "   ##########   ",
  "   #P***#**1#   ",
  " ###*##*#*#*### ",
  " #********#***# ",
  " #*#*#*##*#*#*# ",
  " #***#********# ",
  " ###*#*#*##*### ",
  "   #***#***2#   ",
  "   ##########   ",
  "                ",
  "                ",
  "                ",
  },
  {
  "   ######       ",
  "   #P***####### ",
  "   #*##*#****1# ",
  " ###*##*#*#*#*# ",
  " #**********#*# ",
  " #*#*#*##*#*#*# ",
  " #*#**********# ",
  " #*#*#*#*##*### ",
  " #2****#*##*#   ",
  " #######***3#   ",
  "       ######   ",
  "                ",
  "                ",
  },
  {
  "  ############  ",
  "  #P********1#  ",
  "###*##*##*##*###",
  "#**************#",
  "##*#*###*#*#*#*#",
  "#*****#********#",
  "#*###*#*#*###*##",
  "#*******#******#",
  "##*#*#*###*#*#*#",
  "#**************#",
  "###*##*##*##*###",
  "  #2********3#  ",
  "  ############  ",
  }
};

byte map[MAP_W * MAP_H];
Actor actors[ACTORS_MAX];
byte actor_n;
byte game_level, game_lives, items_count, items_collected;
byte game_clear, game_done, game_paused, spawn_wait;
word rnd = 0xCACE;
byte frame_cnt;

byte rand8(void) {
  rnd = (word)(rnd * 0x41C6 + 0x5B3F);
  return (byte)(rnd >> 8);
}

byte map_at(byte x, byte y) {
  if (x >= MAP_W || y >= MAP_H) return T_WALL;
  return map[y * MAP_W + x];
}

void map_set(byte x, byte y, byte t) {
  map[y * MAP_W + x] = t;
}

void hide_all_sprites(void);
void setup_level_palette(void);
void setup_palette(void);

void hide_all_sprites(void) {
  byte i;
  for (i = 0; i < 32; i++) hide_sprite(i);
}

/* NES small font: digits @16, A-Z @32, colon/slash */
byte font_code(char ch) {
  if (ch >= '0' && ch <= '9') return (byte)(TILE_DIGIT + (ch - '0'));
  if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
  if (ch >= 'A' && ch <= 'Z') return (byte)(TILE_LETTER + (ch - 'A'));
  if (ch == ':') return TILE_COLON;
  if (ch == '/') return TILE_SLASH;
  return TILE_EMPTY;
}

void put_char_pal(byte x, byte y, char ch, byte pal) {
  set_tile(x, y, font_code(ch), pal, 0, 0);
}

void put_char(byte x, byte y, char ch) {
  put_char_pal(x, y, ch, PAL_HUD);
}

void put_string(byte x, byte y, const char* s) {
  while (*s) put_char(x++, y, *s++);
}

void put_digit(byte x, byte y, byte d) {
  set_tile(x, y, (word)(TILE_DIGIT + (d % 10)), PAL_HUD, 0, 0);
}

/* NES attribute byte → MCR BG palette (0-3) for a nametable tile */
byte attr_pal(const byte* attr, byte tx, byte ty) {
  byte b = attr[(byte)((ty >> 2) * 8 + (tx >> 2))];
  byte shift = (byte)(((ty & 2) << 1) | (tx & 2));
  return (byte)((b >> shift) & 3);
}

void blit_nametable(const byte* nt, const byte* attr) {
  byte tx, ty;
  word i = 0;
  for (ty = 0; ty < TILE_ROWS; ty++) {
    for (tx = 0; tx < TILE_COLS; tx++) {
      byte code = nt[i++];
      set_tile(tx, ty, code, attr_pal(attr, tx, ty), 0, 0);
    }
  }
}

void apply_bg_rgb(const byte* rgb48) {
  /* MAME 4bpp: 4 banks × 16 pens. NES supplies 4×4 — map into pens 0-3 of each bank. */
  byte pal, pen;
  for (pal = 0; pal < 4; pal++) {
    for (pen = 0; pen < 4; pen++) {
      byte i = (byte)(pal * 4 + pen);
      set_color((byte)(pal * 16 + pen), rgb48[i * 3], rgb48[i * 3 + 1], rgb48[i * 3 + 2]);
    }
  }
}

void apply_spr_rgb(void) {
  byte i;
  /*
   * 91464: attrib 0 → bank 48 (player), attrib 1 → bank 32 (enemies).
   * Player: pens 5-7 from NES spr pal 0.
   * Enemies: three copies of the same shape use pens 5-7 / 9-11 / 13-15
   * from NES spr pals 1/2/3 — keeps BG pens 0-3 free in both banks.
   */
  const byte* p;
  /* player @ 48 */
  p = &chase_pal_spr_rgb[0];
  set_color(48 + 5, p[3], p[4], p[5]);
  set_color(48 + 6, p[6], p[7], p[8]);
  set_color(48 + 7, p[9], p[10], p[11]);
  /* enemies @ 32 — three pen bands */
  for (i = 0; i < 3; i++) {
    byte base = (byte)(32 + 5 + i * 4);
    p = &chase_pal_spr_rgb[(i + 1) * 12];
    set_color(base,     p[3], p[4], p[5]);
    set_color(base + 1, p[6], p[7], p[8]);
    set_color(base + 2, p[9], p[10], p[11]);
  }
}

byte wall_pal_at(byte x, byte y) {
  return chase_wall_pal[game_level][y * MAP_W + x];
}

void clrscr(void) {
  word i;
  for (i = 0; i < 0x800; i += 2) {
    vram[i] = TILE_EMPTY;
    vram[i + 1] = 0;
  }
  hide_all_sprites();
}

void set_cell_tiles(byte mx, byte my, byte tl, byte tr, byte bl, byte br, byte pal) {
  /* One base address + fixed deltas — avoids SDCC inline set_tile reg clobber
   * that wrote every BR tile to vram[0] ($F000) and left the cell BR blank. */
  byte sx = (byte)(mx << 1);
  byte sy = (byte)((byte)(MAP_Y0 + my) << 1);
  byte attr = (byte)((pal & 3) << 4);
  word base = (word)(((word)sy << 5) + sx) << 1; /* (sy*32+sx)*2 */
  vram[base]      = tl;  vram[base + 1] = attr;
  vram[base + 2]  = tr;  vram[base + 3] = attr;
  vram[base + 64] = bl;  vram[base + 65] = attr; /* +1 tile row */
  vram[base + 66] = br;  vram[base + 67] = attr;
}

void draw_cell(byte x, byte y) {
  byte t = map_at(x, y);
  byte spark = (frame_cnt & 16) != 0;
  byte wpal = wall_pal_at(x, y);
  if (t == T_WALL)
    set_cell_tiles(x, y, TILE_WALL_TL, TILE_WALL_TR, TILE_WALL_BL, TILE_WALL_BR, wpal);
  else if (t == T_ITEM) {
    /* Same pal as floor: pens 0-1 = black+bg speckles, 2-3 = gem colours */
    if (spark)
      set_cell_tiles(x, y, TILE_GEM1_TL, TILE_GEM1_TR, TILE_GEM1_BL, TILE_GEM1_BR, PAL_GEM);
    else
      set_cell_tiles(x, y, TILE_GEM0_TL, TILE_GEM0_TR, TILE_GEM0_BL, TILE_GEM0_BR, PAL_GEM);
  } else if (t == T_FLOOR)
    /* Floor CHR is pens 0-1 only; must share PAL_GEM with items (NES attrs). */
    set_cell_tiles(x, y, TILE_FLOOR, TILE_FLOOR, TILE_FLOOR, TILE_FLOOR, PAL_GEM);
  else
    set_cell_tiles(x, y, TILE_EMPTY, TILE_EMPTY, TILE_EMPTY, TILE_EMPTY, 0);
}

void draw_hud(void) {
  /* NES statsStr @ row 1 col 2: LEVEL:  GEMS:   /    LIVES: */
  put_string(2, 1, "LEVEL:");
  put_digit(8, 1, game_level + 1);
  put_string(10, 1, "GEMS:");
  put_digit(15, 1, items_collected / 100);
  put_digit(16, 1, (items_collected / 10) % 10);
  put_digit(17, 1, items_collected % 10);
  put_char(18, 1, '/');
  put_digit(19, 1, items_count / 100);
  put_digit(20, 1, (items_count / 10) % 10);
  put_digit(21, 1, items_count % 10);
  put_string(23, 1, "LIVES:");
  put_digit(29, 1, game_lives > 0 ? (byte)(game_lives - 1) : 0);
}

byte can_enter(byte tx, byte ty) {
  byte t = map_at(tx, ty);
  return t == T_FLOOR || t == T_ITEM;
}

void actor_try_dir(byte id, byte dir) {
  Actor* a = &actors[id];
  byte tx = POS_TO_TILE(a->x);
  byte ty = POS_TO_TILE(a->y);
  if (dir == DIR_LEFT) tx--;
  else if (dir == DIR_RIGHT) tx++;
  else if (dir == DIR_UP) ty--;
  else if (dir == DIR_DOWN) ty++;
  else return;
  if (!can_enter(tx, ty)) return;
  a->dir = dir;
  a->cnt = (word)TILE_SIZE << FP_BITS;
}

void try_collect(byte id) {
  byte tx, ty;
  Actor* a = &actors[id];
  if (id != 0 || a->wait) return;
  tx = POS_TO_TILE(a->x);
  ty = POS_TO_TILE(a->y);
  if (map_at(tx, ty) != T_ITEM) return;
  map_set(tx, ty, T_FLOOR);
  draw_cell(tx, ty);
  items_collected++;
  draw_hud();
  sfx_play(sfx_item);
}

void load_level(byte li) {
  byte x, y, i;
  const char* row;
  actor_n = 0;
  items_count = 0;
  items_collected = 0;
  game_level = li;
  setup_level_palette();
  clrscr();
  for (y = 0; y < MAP_H; y++) {
    row = levels[li][y];
    for (x = 0; x < MAP_W; x++) {
      char c = row[x];
      byte t = T_BLANK;
      if (c == '#') t = T_WALL;
      else if (c == '*') { t = T_ITEM; items_count++; }
      else if (c == 'P' || c == '1' || c == '2' || c == '3') {
        Actor* a = &actors[actor_n];
        a->x = TILE_TO_POS(x);
        a->y = TILE_TO_POS(y);
        a->cnt = 0;
        a->dir = DIR_NONE;
        if (c == 'P') {
          a->kind = 0;
          a->speed = (word)(2 << FP_BITS);
          a->wait = 16;
        } else {
          a->kind = (byte)(c - '0');
          a->speed = (word)(10 + ((a->kind - 1) << 1));
          a->wait = (byte)(16 + (a->kind << 4));
        }
        actor_n++;
        t = T_FLOOR;
      } else if (c == '.') {
        t = T_FLOOR;
      } else {
        t = T_BLANK;
      }
      map[y * MAP_W + x] = t;
    }
  }
  for (y = 0; y < MAP_H; y++)
    for (x = 0; x < MAP_W; x++)
      draw_cell(x, y);
  for (i = actor_n; i < ACTORS_MAX; i++) hide_sprite(i);
  spawn_wait = (byte)(actor_n << 4);
}

void draw_actors(void) {
  byte i;
  for (i = 0; i < actor_n; i++) {
    Actor* a = &actors[i];
    /* NES pixels → MCR pixels (×2); Y includes MAP_Y0 like NES nametable */
    word px = (word)((a->x >> FP_BITS) * 2);
    word py = (word)((MAP_Y0 * TILE_SIZE + (a->y >> FP_BITS)) * 2);
    byte shape, pal;
    if (a->wait && (a->wait >= 16 || (a->wait & 2))) {
      hide_sprite(i);
      continue;
    }
    if (a->kind == 0) {
      shape = (frame_cnt & 8) ? SPR_PLAYER2 : SPR_PLAYER;
      pal = 0; /* timber attrib 0 → color bank 48 */
    } else {
      /* kind 1/2/3 → SPR_ENEMY1/2/3 (distinct pen bands @ bank 32) */
      shape = (byte)(SPR_ENEMY1 + (a->kind - 1));
      if (shape > SPR_ENEMY3) shape = SPR_ENEMY3;
      pal = 1;
    }
    set_sprite_xy(i, shape, pal, px, py);
  }
}

byte hit_player(void) {
  byte i;
  word px = actors[0].x >> FP_BITS;
  word py = actors[0].y >> FP_BITS;
  if (actors[0].wait) return 0;
  for (i = 1; i < actor_n; i++) {
    word ex, ey;
    if (actors[i].wait) continue;
    ex = actors[i].x >> FP_BITS;
    ey = actors[i].y >> FP_BITS;
    /* NES 4..12 box inside 16×16 */
    if (!((px + 4) >= (ex + 12) || (ex + 4) >= (px + 12) ||
          (py + 4) >= (ey + 12) || (ey + 4) >= (py + 12)))
      return 1;
  }
  return 0;
}

void animate_gems(void) {
  byte x, y;
  if ((frame_cnt & 15) != 0) return;
  for (y = 0; y < MAP_H; y++)
    for (x = 0; x < MAP_W; x++)
      if (map_at(x, y) == T_ITEM) draw_cell(x, y);
}

void enemy_ai(byte id) {
  Actor* a = &actors[id];
  byte tx = POS_TO_TILE(a->x);
  byte ty = POS_TO_TILE(a->y);
  byte dirs[4];
  byte n = 0;
  byte prev = a->dir;
  if (prev != DIR_RIGHT && can_enter((byte)(tx - 1), ty)) dirs[n++] = DIR_LEFT;
  if (prev != DIR_LEFT  && can_enter((byte)(tx + 1), ty)) dirs[n++] = DIR_RIGHT;
  if (prev != DIR_DOWN  && can_enter(tx, (byte)(ty - 1))) dirs[n++] = DIR_UP;
  if (prev != DIR_UP    && can_enter(tx, (byte)(ty + 1))) dirs[n++] = DIR_DOWN;
  if (!n) return;
  actor_try_dir(id, dirs[rand8() % n]);
  if (n > 1) {
    Actor* p = &actors[0];
    if (prev != DIR_DOWN && p->y < a->y) actor_try_dir(id, DIR_UP);
    if (prev != DIR_UP   && p->y > a->y) actor_try_dir(id, DIR_DOWN);
    if (prev != DIR_RIGHT && p->x < a->x) actor_try_dir(id, DIR_LEFT);
    if (prev != DIR_LEFT  && p->x > a->x) actor_try_dir(id, DIR_RIGHT);
  }
}

void player_controls(void) {
  byte j = 0;
  Actor* a = &actors[0];
  if (joy_left) j |= DIR_LEFT;
  if (joy_right) j |= DIR_RIGHT;
  if (joy_up) j |= DIR_UP;
  if (joy_down) j |= DIR_DOWN;
  if (j & a->dir) {
    j = (byte)(j & ~a->dir);
    actor_try_dir(0, a->dir);
  }
  if (j & DIR_LEFT) actor_try_dir(0, DIR_LEFT);
  if (j & DIR_RIGHT) actor_try_dir(0, DIR_RIGHT);
  if (j & DIR_UP) actor_try_dir(0, DIR_UP);
  if (j & DIR_DOWN) actor_try_dir(0, DIR_DOWN);
}

void advance_actor(byte id) {
  Actor* a = &actors[id];
  word step;
  if (!a->cnt) return;
  step = a->speed;
  if (step > a->cnt) step = a->cnt;
  if (a->dir == DIR_LEFT) a->x -= step;
  else if (a->dir == DIR_RIGHT) a->x += step;
  else if (a->dir == DIR_UP) a->y -= step;
  else if (a->dir == DIR_DOWN) a->y += step;
  a->cnt -= step;
  if (!a->cnt) {
    /* Snap to tile grid. Step is clamped so we never overrun; the NES
     * left/up +0x100 nudge is only for negative-cnt overshoot and would
     * bounce us back a tile here. */
    a->x &= 0xff00;
    a->y &= 0xff00;
    try_collect(id);
  }
}

void wait_frame(void) {
  wait_vblank();
  sfx_update();
}

void wait_frames(byte n) {
  while (n--) wait_frame();
}

void wait_for_fire(void) {
  read_controls();
  while (!joy_fire) { wait_frame(); read_controls(); }
  while (joy_fire) { wait_frame(); read_controls(); }
}

void setup_palette(void) {
  apply_bg_rgb(chase_pal_game0_rgb);
  apply_spr_rgb();
}

void setup_level_palette(void) {
  apply_bg_rgb(chase_pal_game_rgb[game_level]);
  apply_spr_rgb();
}

void patch_large_digit(byte digit /*1-5*/, byte col, byte row) {
  /* Direct VRAM — avoid SDCC set_tile pair clobber (same class of bug as BR tiles). */
  byte j = (byte)((digit - 1) << 1);
  byte r;
  byte attr = (byte)((PAL_HUD & 3) << 4);
  /* Large digits use pen 3; LEVEL letters use pen 2 — both forced bright below. */
  for (r = 0; r < 3; r++) {
    word base = (word)(((word)(row + r) << 5) + col) << 1;
    vram[base]     = chase_large_nums[j];
    vram[base + 1] = attr;
    vram[base + 2] = chase_large_nums[j + 1];
    vram[base + 3] = attr;
    j = (byte)(j + 10);
  }
}

void title_screen(void) {
  byte blink = 0;
  byte wait;

  hide_all_sprites();
  apply_bg_rgb(chase_pal_title_rgb);
  apply_spr_rgb();
  blit_nametable(chase_title_nt, chase_title_attr);

  wait = 160;
  frame_cnt = 0;
  read_controls();
  while (joy_fire) { wait_frame(); read_controls(); }

  while (1) {
    wait_frame();
    read_controls();
    if (joy_fire) {
      sfx_play(sfx_start);
      break;
    }

    if (wait) {
      --wait;
    } else {
      blink = (byte)((frame_cnt >> 4) & 1);
      set_color(2, blink ? 5 : 0, blink ? 5 : 0, blink ? 5 : 0);
      frame_cnt++;
    }
  }
  while (joy_fire) { wait_frame(); read_controls(); }
  for (blink = 0; blink < 16; blink++) {
    wait_frame();
    set_color(2, (blink & 1) ? 7 : 0, (blink & 1) ? 7 : 0, (blink & 1) ? 7 : 0);
  }
}

void show_level_banner(void) {
  hide_all_sprites();
  apply_bg_rgb(chase_pal_game_rgb[game_level]);
  blit_nametable(chase_level_scr_nt, chase_level_scr_attr);
  /* Letters = pen 2 (white); large digit = pen 3 (gold) — distinct like NES accents */
  set_color(2, 7, 7, 7);
  set_color(3, 7, 6, 0);
  patch_large_digit((byte)(game_level + 1), 20, 12);
  wait_frames(50);
}

void show_game_over(void) {
  hide_all_sprites();
  apply_bg_rgb(chase_pal_game0_rgb);
  blit_nametable(chase_gameover_nt, chase_gameover_attr);
  set_color(3, 7, 7, 7);
  read_controls();
  while (joy_fire) { wait_frame(); read_controls(); }
  frame_cnt = 0;
  while (1) {
    wait_frame();
    frame_cnt++;
    /* flash color 2: NES 0x25 ↔ 0x15 */
    if (frame_cnt & 2) set_color(2, 6, 3, 5);
    else set_color(2, 5, 1, 4);
    read_controls();
    if (joy_fire) break;
  }
  while (joy_fire) { wait_frame(); read_controls(); }
}

void show_well_done(void) {
  hide_all_sprites();
  apply_bg_rgb(chase_pal_game0_rgb);
  blit_nametable(chase_welldone_nt, chase_welldone_attr);
  set_color(3, 7, 7, 7);
  read_controls();
  while (joy_fire) { wait_frame(); read_controls(); }
  frame_cnt = 0;
  while (1) {
    wait_frame();
    frame_cnt++;
    /* flash: NES 0x21 ↔ 0x11 */
    if (frame_cnt & 2) set_color(2, 2, 4, 7);
    else set_color(2, 0, 3, 6);
    read_controls();
    if (joy_fire) break;
  }
  while (joy_fire) { wait_frame(); read_controls(); }
}

void game_loop(void) {
  byte i;
  hide_all_sprites();
  load_level(game_level);
  draw_hud();
  game_done = 0;
  game_clear = 0;
  game_paused = 0;
  frame_cnt = 0;
  fire_prev = 1;
  game_paused = 0;
  while (!game_done) {
    wait_frame();
    frame_cnt++;
    read_controls();
    if (joy_fire && !fire_prev) {
      game_paused = !game_paused;
      draw_hud();
    }
    fire_prev = joy_fire;
    if (game_paused) { draw_actors(); continue; }
    animate_gems();

    if (items_collected >= items_count && !game_clear) {
      game_clear = 1;
      game_done = 1;
    }

    if (spawn_wait) --spawn_wait;

    for (i = 0; i < actor_n; i++) {
      if (actors[i].wait) {
        actors[i].wait--;
        continue;
      }
      if (spawn_wait) continue;
      advance_actor(i);
      if (!actors[i].cnt) {
        if (i == 0) player_controls();
        else enemy_ai(i);
      }
    }
    draw_actors();
    if (!game_clear && hit_player()) {
      sfx_play(sfx_hit);
      game_done = 1;
      wait_frames(60);
    }
  }
  if (game_clear) wait_frames(50);
  hide_all_sprites();
}

void main(void) {
  mcr_init();
  sound_init();
  setup_palette();
  (void)chase_bg_gfx[0];
  (void)chase_spr_gfx[0];
  (void)chase_editor_pal[0];

  while (1) {
    title_screen();
    game_level = 0;
    game_lives = 4;
    while (game_lives && game_level < LEVELS_ALL) {
      show_level_banner();
      game_loop();
      if (game_clear) game_level++;
      else game_lives--;
    }
    if (!game_lives) show_game_over();
    else show_well_done();
  }
}
