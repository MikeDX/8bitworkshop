/*
 * Solarian (vertical cabinet) — Midway MCR (91490 / timber).
 * Logic from presets/galaxian-scramble/shoot2.c, oriented for portrait play:
 *
 *   Landscape FB: ship on RIGHT, formation on LEFT, fire -X, strafe along Y.
 *   Source tag rotate:90 → CSS canvas rotate; ship at visual bottom.
 *   Gfx (solarian_gfx_v.h): art rotated 90° CW so ships point -X (left).
 *
 * Text advances along FB Y so strings read across after rotate.
 * 91490 has no BG scroll (MAME timber) — formation motion is tile redraw only.
 */
/*{rotate:90}*/
#include "mcr.h"
#include "solarian_gfx_v.h"

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

#define ENEMIES_PER_ROW 8
#define ENEMY_ROWS 4
#define MAX_IN_FORMATION (ENEMIES_PER_ROW * ENEMY_ROWS)
#define MAX_ATTACKERS 6
#define MAX_MISSILES 8
#define PLAYER_MISSILE 7

#define SPR_PLAYER 0
#define SPR_ATK0   1
#define SPR_BULLET 7
#define SPR_BOOM   8

/*
 * Axes after rotate:90:
 *   depth (toward ship / visual down) = +X
 *   across (strafe / visual L-R)      = Y
 * Formation at low X (visual top); ship at high X (visual bottom).
 */
#define FORM_COL0      2  /* tile col for row 0 (visual top) */
#define FORM_ROW0      2
#define FORM_CSPACE    2  /* tile cols between formation rows (toward ship) */
#define FORM_RSPACE    3  /* tile rows between aliens across */
#define TILE_PX       16
#define HIT_W         32
#define PLAYER_X     448

#define FLIPX  0x40
#define FLIPY  0x80
#define FLIPXY 0xc0

typedef struct { byte shape; } FormationEnemy;
typedef struct {
  byte findex;
  byte shape;
  unsigned long x, y;
  byte dir;
  byte returning;
} AttackingEnemy;
typedef struct {
  byte active;
  word x, y;
  sbyte dx, dy;
} Missile;

FormationEnemy formation[MAX_IN_FORMATION];
AttackingEnemy attackers[MAX_ATTACKERS];
Missile missiles[MAX_MISSILES];

word formation_offset_y;
sbyte formation_direction;
byte current_row;
word player_y;
byte player_exploding;
byte enemy_exploding;
word boom_x, boom_y;
byte enemies_left;
word player_score;
word framecount;
byte lives;

/* Same table as galaxian; dive dir 0 maps to −X (toward ship on left) */
static const byte DIR_TO_CODE[32] = {
  0, 1, 2, 3, 4, 5, 6, 6,
  6|FLIPXY, 6|FLIPXY, 5|FLIPXY, 4|FLIPXY, 3|FLIPXY, 2|FLIPXY, 1|FLIPXY, 0|FLIPXY,
  0|FLIPX, 1|FLIPX, 2|FLIPX, 3|FLIPX, 4|FLIPX, 5|FLIPX, 6|FLIPX, 6|FLIPX,
  6|FLIPY, 6|FLIPY, 5|FLIPY, 4|FLIPY, 3|FLIPY, 2|FLIPY, 1|FLIPY, 0|FLIPY,
};

static const int SINTBL2[32] = {
  0, 50, 98, 142, 180, 212, 234, 250,
  254, 250, 234, 212, 180, 142, 98, 50,
  0, -50, -98, -142, -180, -212, -234, -250,
  -254, -250, -234, -212, -180, -142, -98, -50,
};

static word lfsr = 1;

word rand16(void) {
  byte lsb = (byte)(lfsr & 1);
  lfsr >>= 1;
  if (lsb) lfsr ^= 0xB400;
  return lfsr;
}

#define PIX(fp) ((word)((fp) >> 8))

void wait_frame(void) { wait_vblank(); }

void clrscr(void) {
  word i;
  for (i = 0; i < 0x800; i += 2) {
    vram[i] = T_BLANK;
    vram[i + 1] = 0;
  }
  hide_all_sprites();
}

void poke_tile(byte x, byte y, word code, byte pal) {
  if (x >= TILE_COLS || y >= TILE_ROWS) return;
  set_tile(x, y, code, pal, 0, 0);
}

byte font_code(char ch) {
  if (ch >= '0' && ch <= '9') return (byte)(T_DIGIT + (ch - '0'));
  if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
  if (ch >= 'A' && ch <= 'Z') return (byte)(T_LETTER + (ch - 'A'));
  return T_BLANK;
}

void put_char(byte x, byte y, char ch, byte pal) {
  poke_tile(x, y, font_code(ch), pal);
}

/* After rotate:90, visual L→R ≈ −Y on the framebuffer */
void put_string(byte x, byte y, const char* s, byte pal) {
  while (*s) put_char(x, y--, *s++, pal);
}

void put_digit(byte x, byte y, byte d, byte pal) {
  poke_tile(x, y, (word)(T_DIGIT + (d & 15)), pal);
}

word bcd_add(word a, word b) {
  word r = 0;
  byte i, carry = 0;
  for (i = 0; i < 4; i++) {
    byte n = (byte)((a & 15) + (b & 15) + carry);
    if (n > 9) { n = (byte)(n - 10); carry = 1; }
    else carry = 0;
    r |= (word)n << (i * 4);
    a >>= 4;
    b >>= 4;
  }
  return r;
}

void apply_palette(void) {
  byte i;
  static const byte bg[48] = {
    0,0,0, 7,7,7, 0,6,6, 7,7,0,
    0,0,0, 0,5,5, 0,7,7, 7,7,7,
    0,0,0, 6,2,5, 7,4,6, 7,7,7,
    0,0,0, 7,4,0, 7,6,0, 7,7,7,
  };
  for (i = 0; i < 16; i++)
    set_color((byte)((i / 4) * 16 + (i % 4)), bg[i * 3], bg[i * 3 + 1], bg[i * 3 + 2]);
  set_color(48 + 5, 7, 7, 0);
  set_color(48 + 6, 7, 5, 0);
  set_color(48 + 7, 7, 7, 4);
  set_color(32 + 5, 0, 5, 7);
  set_color(32 + 6, 7, 4, 0);
  set_color(32 + 7, 7, 7, 7);
}

/* HUD near visual top = low FB X; no scroll compensation needed */
void draw_score(void) {
  byte i;
  word s = player_score;
  put_string(1, 20, "SC", PAL_CYAN);
  for (i = 0; i < 4; i++) {
    put_digit(1, (byte)(17 - i), (byte)((s >> 12) & 0xf), PAL_HUD);
    s <<= 4;
  }
  put_string(3, 20, "LV", PAL_CYAN);
  put_digit(3, 17, lives, PAL_HUD);
}

void add_score(word bcd) {
  player_score = bcd_add(player_score, bcd);
  draw_score();
}

void setup_formation(void) {
  byte i;
  for (i = 0; i < MAX_IN_FORMATION; i++) formation[i].shape = 1;
  for (i = 0; i < MAX_ATTACKERS; i++) attackers[i].findex = 0;
  for (i = 0; i < MAX_MISSILES; i++) missiles[i].active = 0;
  enemies_left = MAX_IN_FORMATION;
  formation_offset_y = 0;
  formation_direction = 1;
  current_row = 0;
}

word get_attacker_x(byte fi) {
  byte row = (byte)(fi / ENEMIES_PER_ROW);
  return (word)((FORM_COL0 + row * FORM_CSPACE) * TILE_PX);
}

word get_attacker_y(byte fi) {
  byte col = (byte)(fi % ENEMIES_PER_ROW);
  byte yoff = (byte)((formation_offset_y * 2) / TILE_PX);
  return (word)((FORM_ROW0 + yoff + col * FORM_RSPACE) * TILE_PX);
}

void set_spr_flip(byte i, byte code) {
  byte* p = &sprram[(word)i * 4];
  if (code & FLIPX) p[1] |= 0x10;
  if (code & FLIPY) p[1] |= 0x20;
}

void draw_formation_row(byte row) {
  byte i;
  byte tx = (byte)(FORM_COL0 + row * FORM_CSPACE);
  byte yoff = (byte)((formation_offset_y * 2) / TILE_PX);
  byte frame = (byte)((framecount >> 4) & 1);
  byte t0 = frame ? T_FORM_B0 : T_FORM_A0;
  byte t1 = frame ? T_FORM_B1 : T_FORM_A1;
  byte pal = (row == 0) ? PAL_PINK : PAL_CYAN;
  /* Clear this formation column (2 tile rows tall per alien) */
  for (i = 0; i < TILE_ROWS; i++) poke_tile(tx, i, T_BLANK, 0);
  for (i = 0; i < ENEMIES_PER_ROW; i++) {
    byte ty = (byte)(FORM_ROW0 + yoff + i * FORM_RSPACE);
    if (ty >= TILE_ROWS - 1) continue;
    if (formation[i + row * ENEMIES_PER_ROW].shape) {
      poke_tile(tx, ty, t0, pal);
      poke_tile(tx, (byte)(ty + 1), t1, pal);
    }
  }
}

void draw_next_row(void) {
  draw_formation_row(current_row);
  if (++current_row == ENEMY_ROWS) {
    current_row = 0;
    formation_offset_y = (word)(formation_offset_y + formation_direction);
    if (formation_offset_y >= 40) formation_direction = -1;
    else if (formation_offset_y == 0) formation_direction = 1;
  }
}

void draw_attacker(byte i) {
  AttackingEnemy* a = &attackers[i];
  if (a->findex) {
    byte code = DIR_TO_CODE[a->dir & 31];
    byte frame = (byte)(S_ATK0 + (code & 7));
    set_sprite_xy((byte)(SPR_ATK0 + i), frame, 1, PIX(a->x), PIX(a->y));
    set_spr_flip((byte)(SPR_ATK0 + i), code);
  } else {
    hide_sprite((byte)(SPR_ATK0 + i));
  }
}

void draw_attackers(void) {
  byte i;
  for (i = 0; i < MAX_ATTACKERS; i++) draw_attacker(i);
}

void return_attacker(AttackingEnemy* a) {
  byte fi = (byte)(a->findex - 1);
  word destx = get_attacker_x(fi);
  word desty = get_attacker_y(fi);
  word x = PIX(a->x);
  word xdist = (x >= destx) ? (x - destx) : (destx - x);
  if (xdist < 4) {
    formation[fi].shape = a->shape;
    a->findex = 0;
  } else {
    a->dir = (byte)((xdist + 16) & 31);
    a->y = ((unsigned long)desty) << 8;
    a->x -= 128; /* climb back toward formation (−X) */
  }
}

void fly_attacker(AttackingEnemy* a) {
  /* Dive +X toward ship on the right; strafe on Y */
  a->x = (unsigned long)((long)a->x + SINTBL2[(a->dir + 8) & 31]);
  a->y = (unsigned long)((long)a->y + SINTBL2[a->dir & 31]);
  if (PIX(a->x) < 8 || PIX(a->x) > 470) a->returning = 1;
}

void move_attackers(void) {
  byte i;
  for (i = 0; i < MAX_ATTACKERS; i++) {
    AttackingEnemy* a = &attackers[i];
    if (!a->findex) continue;
    if (a->returning) return_attacker(a);
    else fly_attacker(a);
  }
}

void think_attackers(void) {
  byte i;
  for (i = 0; i < MAX_ATTACKERS; i++) {
    AttackingEnemy* a = &attackers[i];
    word x, y;
    if (!a->findex) continue;
    x = PIX(a->x);
    y = PIX(a->y);
    /* Far from ship (low X) or exploding → turn; else shoot toward ship */
    if (x < 280 || player_exploding) {
      if (y < 240) a->dir++;
      else a->dir--;
    } else if (i < PLAYER_MISSILE && !missiles[i].active) {
      missiles[i].active = 1;
      missiles[i].x = x + 24;
      missiles[i].y = y + 8;
      missiles[i].dx = 4;
      missiles[i].dy = 0;
    }
  }
}

void formation_to_attacker(byte fi) {
  byte i;
  if (fi >= MAX_IN_FORMATION || !formation[fi].shape) return;
  for (i = 0; i < MAX_ATTACKERS; i++) {
    AttackingEnemy* a = &attackers[i];
    if (a->findex == 0) {
      a->x = ((unsigned long)get_attacker_x(fi)) << 8;
      a->y = ((unsigned long)get_attacker_y(fi)) << 8;
      a->shape = formation[fi].shape;
      a->findex = (byte)(fi + 1);
      a->dir = 0;
      a->returning = 0;
      formation[fi].shape = 0;
      break;
    }
  }
}

void new_attack_wave(void) {
  byte i = (byte)(rand16() & (MAX_IN_FORMATION - 1));
  byte j;
  for (j = 0; j < MAX_IN_FORMATION; j++) {
    i = (byte)((i + 1) & (MAX_IN_FORMATION - 1));
    if (formation[i].shape) {
      formation_to_attacker(i);
      formation_to_attacker((byte)(i + 1));
      formation_to_attacker((byte)(i + ENEMIES_PER_ROW));
      formation_to_attacker((byte)(i + ENEMIES_PER_ROW + 1));
      break;
    }
  }
}

void erase_enemy_missiles(void) {
  byte i;
  for (i = 0; i < PLAYER_MISSILE; i++) {
    byte tx, ty;
    if (!missiles[i].active) continue;
    tx = (byte)(missiles[i].x >> 4);
    ty = (byte)(missiles[i].y >> 4);
    if (tx < TILE_COLS && ty < TILE_ROWS) poke_tile(tx, ty, T_BLANK, 0);
  }
}

void draw_missiles(void) {
  byte i;
  for (i = 0; i < PLAYER_MISSILE; i++) {
    byte tx, ty;
    if (!missiles[i].active) continue;
    tx = (byte)(missiles[i].x >> 4);
    ty = (byte)(missiles[i].y >> 4);
    if (tx < TILE_COLS && ty < TILE_ROWS)
      poke_tile(tx, ty, T_BULLET_T, PAL_ORANGE);
  }
  if (missiles[PLAYER_MISSILE].active)
    set_sprite_xy(SPR_BULLET, S_BULLET, 0, missiles[PLAYER_MISSILE].x, missiles[PLAYER_MISSILE].y);
  else
    hide_sprite(SPR_BULLET);
}

void move_missiles(void) {
  byte i;
  erase_enemy_missiles();
  for (i = 0; i < MAX_MISSILES; i++) {
    if (!missiles[i].active) continue;
    if (missiles[i].dx < 0) {
      byte step = (byte)(-missiles[i].dx);
      if (missiles[i].x < step) { missiles[i].active = 0; continue; }
      missiles[i].x -= step;
    } else if (missiles[i].dx > 0) {
      missiles[i].x += (byte)missiles[i].dx;
      if (missiles[i].x > 500) missiles[i].active = 0;
    }
  }
}

void move_player(void) {
  /* After rotate:90: visual L = +Y, visual R = −Y */
  if (LEFT1 && player_y < 448) player_y += 2;
  if (RIGHT1 && player_y > 16) player_y -= 2;
  if ((FIRE1 || START1) && !missiles[PLAYER_MISSILE].active) {
    missiles[PLAYER_MISSILE].active = 1;
    missiles[PLAYER_MISSILE].x = PLAYER_X - 8;
    missiles[PLAYER_MISSILE].y = player_y + 8;
    missiles[PLAYER_MISSILE].dx = -6;
    missiles[PLAYER_MISSILE].dy = 0;
  }
  if (!player_exploding)
    set_sprite_xy(SPR_PLAYER, S_PLAYER, 0, PLAYER_X, player_y);
}

char in_rect(word x, word y, word x0, word y0, word w, word h) {
  return ((word)(x - x0) < w && (word)(y - y0) < h);
}

void blowup_at(word x, word y) {
  boom_x = x;
  boom_y = y;
  set_sprite_xy(SPR_BOOM, S_BOOM1, 1, x, y);
  enemy_exploding = 1;
}

void animate_boom(void) {
  if (!enemy_exploding) return;
  enemy_exploding++;
  if (enemy_exploding > 8) {
    enemy_exploding = 0;
    hide_sprite(SPR_BOOM);
  } else {
    set_sprite_xy(SPR_BOOM, (enemy_exploding & 1) ? S_BOOM1 : S_BOOM2, 1, boom_x, boom_y);
  }
}

void hide_player_missile(void) {
  missiles[PLAYER_MISSILE].active = 0;
}

void does_player_shoot_formation(void) {
  word mx, my;
  signed char row;
  byte column, localy, index;
  word yoffset;
  word form_x0 = (word)(FORM_COL0 * TILE_PX);
  word form_y0 = (word)((FORM_ROW0 + (formation_offset_y * 2) / TILE_PX) * TILE_PX);
  word xspace = (word)(FORM_CSPACE * TILE_PX);
  word yspace = (word)(FORM_RSPACE * TILE_PX);
  if (!missiles[PLAYER_MISSILE].active) return;
  mx = missiles[PLAYER_MISSILE].x;
  my = missiles[PLAYER_MISSILE].y;
  row = (signed char)((mx - form_x0) / xspace);
  if (row < 0 || row >= ENEMY_ROWS) return;
  yoffset = (word)(my - form_y0);
  column = (byte)(yoffset / yspace);
  localy = (byte)(yoffset - column * yspace);
  if (column < ENEMIES_PER_ROW && localy < HIT_W) {
    index = (byte)(column + row * ENEMIES_PER_ROW);
    if (formation[index].shape) {
      formation[index].shape = 0;
      enemies_left--;
      blowup_at(get_attacker_x(index), get_attacker_y(index));
      hide_player_missile();
      add_score(0x0002);
    }
  }
}

void does_player_shoot_attacker(void) {
  byte i;
  word mx, my;
  if (!missiles[PLAYER_MISSILE].active) return;
  mx = missiles[PLAYER_MISSILE].x;
  my = missiles[PLAYER_MISSILE].y;
  for (i = 0; i < MAX_ATTACKERS; i++) {
    AttackingEnemy* a = &attackers[i];
    if (a->findex && in_rect(mx, my, PIX(a->x), PIX(a->y), HIT_W, HIT_W)) {
      blowup_at(PIX(a->x), PIX(a->y));
      a->findex = 0;
      enemies_left--;
      hide_player_missile();
      add_score(0x0005);
      break;
    }
  }
}

void does_missile_hit_player(void) {
  byte i;
  if (player_exploding) return;
  for (i = 0; i < PLAYER_MISSILE; i++) {
    if (missiles[i].active &&
        in_rect(missiles[i].x, missiles[i].y, PLAYER_X, player_y, HIT_W, HIT_W)) {
      player_exploding = 1;
      missiles[i].active = 0;
      return;
    }
  }
  for (i = 0; i < MAX_ATTACKERS; i++) {
    AttackingEnemy* a = &attackers[i];
    if (a->findex && in_rect(PIX(a->x), PIX(a->y), PLAYER_X, player_y, HIT_W, HIT_W)) {
      player_exploding = 1;
      return;
    }
  }
}

void seed_stars(void) {
  byte i;
  for (i = 0; i < 20; i++) {
    byte x = (byte)(rand16() & 31);
    byte y = (byte)(2 + (rand16() % 26));
    poke_tile(x, y, (rand16() & 1) ? T_STAR1 : T_STAR2, PAL_HUD);
  }
}

void play_round(void) {
  byte end_timer = 255;

  player_score = 0;
  lives = 3;
  clrscr();
  apply_palette();
  seed_stars();
  draw_score();

  setup_formation();
  player_y = 224;
  player_exploding = 0;
  enemy_exploding = 0;
  framecount = 0;
  {
    byte r;
    for (r = 0; r < ENEMY_ROWS; r++) draw_formation_row(r);
  }

  while (end_timer) {
    wait_frame();
    framecount++;

    if (player_exploding) {
      set_sprite_xy(SPR_PLAYER, (framecount & 2) ? S_BOOM1 : S_BOOM2, 1, PLAYER_X, player_y);
      if ((framecount & 31) == 31) {
        player_exploding = 0;
        if (lives) {
          lives--;
          draw_score();
          player_y = 224;
        } else {
          end_timer = 1;
        }
      }
    } else {
      if (((byte)framecount == 0 || enemies_left < 8) && enemies_left > 0)
        new_attack_wave();
      move_player();
      does_missile_hit_player();
    }

    if ((framecount & 3) == 0) animate_boom();
    move_attackers();
    move_missiles();
    does_player_shoot_formation();
    does_player_shoot_attacker();
    draw_next_row();
    draw_attackers();
    draw_missiles();
    if ((framecount & 0xf) == 0) think_attackers();

    if (!enemies_left) end_timer--;
    if (!lives && !player_exploding) end_timer--;
  }

  hide_all_sprites();
  put_string(16, 22, "GAME OVER", PAL_ORANGE);
  {
    byte t = 120;
    while (t--) wait_frame();
  }
}

void main(void) {
  mcr_init();
  apply_palette();
  (void)solarian_bg_gfx[0];
  (void)solarian_spr_gfx[0];

  while (1) {
    clrscr();
    apply_palette();
    put_string(16, 22, "SOLARIAN", PAL_PINK);
    put_string(14, 22, "VERTICAL", PAL_CYAN);
    put_string(12, 22, "PRESS START", PAL_HUD);
    put_string(10, 22, "FIRE TO SHOOT", PAL_ORANGE);
    while (!(FIRE1 || START1)) wait_frame();
    while (FIRE1 || START1) wait_frame();
    play_round();
  }
}
