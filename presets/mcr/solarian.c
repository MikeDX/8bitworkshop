/*
 * Solarian (horizontal) — Midway MCR (91490 / timber).
 * Logic port of presets/galaxian-scramble/shoot2.c (source of truth),
 * scaled ~2× for 32px sprites on a 512×480 framebuffer.
 *
 * Gfx: scripts/gen_mcr_solarian_gfx.py
 * Vertical cabinet: solarian_v.c (separate — not shared).
 */
#include "mcr.h"
#include "solarian_gfx.h"

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
 * Formation is tiled. 91490 has no BG scroll (MAME timber) — smooth X uses
 * NES-style pre-shifted CHR (8 pixel phases × 3 tiles). 1 game-px = 2 screen px.
 */
#define FORM_COL0     2
#define FORM_ROW0     3
#define FORM_CSPACE   3
#define FORM_RSPACE   2
#define TILE_PX       16
#define GAL_HIT       16          /* shoot2 in_rect w/h */
#define PLAYER_Y      432

/* Galaxian shoot2 space (0–255); sprites/tiles are 2× on MCR. */
#define SCR(p) ((word)(p) * 2)
#define PLAYER_X0     SCR(112)    /* shoot2 new_player_ship */
#define GAL_X0  (FORM_COL0 * 8)   /* 16 ≈ shoot2 FORMATION_X0 18 */
#define GAL_Y0  (FORM_ROW0 * 8)   /* 24 ≈ shoot2 FORMATION_Y0 27 */
#define GAL_XS  (FORM_CSPACE * 8) /* 24 */
#define GAL_YS  (FORM_RSPACE * 8) /* 16 */

#define FLIPX  0x40
#define FLIPY  0x80
#define FLIPXY 0xc0

typedef struct { byte shape; } FormationEnemy;
typedef struct {
  byte findex;
  byte shape;
  word x, y; /* 8.8 fixed, galaxian pixel space (shoot2) */
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

word formation_offset_x;
sbyte formation_direction;
byte current_row;
word player_x;
byte player_exploding;
byte enemy_exploding;
word boom_x, boom_y;
byte enemies_left;
word player_score;
word framecount;
byte lives;

/* Galaxian DIR_TO_CODE — dive toward +Y (player at bottom) */
static const byte DIR_TO_CODE[32] = {
  0, 1, 2, 3, 4, 5, 6, 6,
  6|FLIPXY, 6|FLIPXY, 5|FLIPXY, 4|FLIPXY, 3|FLIPXY, 2|FLIPXY, 1|FLIPXY, 0|FLIPXY,
  0|FLIPX, 1|FLIPX, 2|FLIPX, 3|FLIPX, 4|FLIPX, 5|FLIPX, 6|FLIPX, 6|FLIPX,
  6|FLIPY, 6|FLIPY, 5|FLIPY, 4|FLIPY, 3|FLIPY, 2|FLIPY, 1|FLIPY, 0|FLIPY,
};

/* Exact Galaxian shoot2 SINTBL */
static const byte SINTBL[32] = {
  0, 25, 49, 71, 90, 106, 117, 125,
  127, 125, 117, 106, 90, 71, 49, 25,
  0, (byte)-25, (byte)-49, (byte)-71, (byte)-90, (byte)-106, (byte)-117, (byte)-125,
  (byte)-127, (byte)-125, (byte)-117, (byte)-106, (byte)-90, (byte)-71, (byte)-49, (byte)-25,
};

static word lfsr = 1;

word rand16(void) {
  byte lsb = (byte)(lfsr & 1);
  lfsr >>= 1;
  if (lsb) lfsr ^= 0xB400;
  return lfsr;
}

signed char isin(byte dir) {
  return (signed char)SINTBL[dir & 31];
}

signed char icos(byte dir) {
  return isin((byte)(dir + 8));
}

#define PIX(fp) ((byte)((fp) >> 8))

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

void put_string(byte x, byte y, const char* s, byte pal) {
  while (*s) put_char(x++, y, *s++, pal);
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

void draw_score(void) {
  byte i;
  word s = player_score;
  put_string(1, 0, "SCORE", PAL_CYAN);
  for (i = 0; i < 4; i++) {
    put_digit(10 - i, 0, (byte)(s & 0xf), PAL_HUD);
    s >>= 4;
  }
  put_string(18, 0, "LIVES", PAL_CYAN);
  put_digit(24, 0, lives, PAL_HUD);
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
  formation_offset_x = 0;
  formation_direction = 1;
  current_row = 0;
}

byte get_attacker_x(byte fi) {
  byte col = (byte)(fi % ENEMIES_PER_ROW);
  return (byte)(GAL_X0 + formation_offset_x + col * GAL_XS);
}

byte get_attacker_y(byte fi) {
  byte row = (byte)(fi / ENEMIES_PER_ROW);
  return (byte)(GAL_Y0 + row * GAL_YS);
}

void set_spr_flip(byte i, byte code) {
  byte* p = &sprram[(word)i * 4];
  if (code & FLIPX) p[1] |= 0x10;
  if (code & FLIPY) p[1] |= 0x20;
}

void draw_formation_row(byte row) {
  byte i, col;
  byte ty = (byte)(FORM_ROW0 + row * FORM_RSPACE);
  byte shift = (byte)(formation_offset_x & 7);
  byte coarse = (byte)(formation_offset_x >> 3);
  byte frame = (byte)((framecount >> 4) & 1);
  byte tbase = (byte)((frame ? T_FORM_BSH : T_FORM_ASH) + shift * 3);
  byte attr = (byte)(((row == 0) ? PAL_PINK : PAL_CYAN) << 4);
  byte line[32];
  byte* p;

  for (col = 0; col < TILE_COLS; col++) line[col] = T_BLANK;
  for (i = 0; i < ENEMIES_PER_ROW; i++) {
    byte tx;
    if (!formation[i + row * ENEMIES_PER_ROW].shape) continue;
    tx = (byte)(FORM_COL0 + coarse + i * FORM_CSPACE);
    if (tx >= TILE_COLS - 2) continue;
    line[tx] = tbase;
    line[(byte)(tx + 1)] = (byte)(tbase + 1);
    line[(byte)(tx + 2)] = (byte)(tbase + 2);
  }
  p = &vram[(word)ty << 6];
  for (col = 0; col < TILE_COLS; col++) {
    byte c = line[col];
    *p++ = c;
    *p++ = c ? attr : 0;
  }
}

void draw_next_row(void) {
  draw_formation_row(current_row);
  if (++current_row == ENEMY_ROWS) {
    current_row = 0;
    formation_offset_x = (word)(formation_offset_x + formation_direction);
    if (formation_offset_x == 40) formation_direction = -1;
    else if (formation_offset_x == 0) formation_direction = 1;
  }
}

void draw_attacker(byte i) {
  AttackingEnemy* a = &attackers[i];
  if (a->findex) {
    byte code = DIR_TO_CODE[a->dir & 31];
    byte frame = (byte)(S_ATK0 + (code & 7));
    set_sprite_xy((byte)(SPR_ATK0 + i), frame, 1, SCR(PIX(a->x)), SCR(PIX(a->y)));
    set_spr_flip((byte)(SPR_ATK0 + i), code);
  } else {
    hide_sprite((byte)(SPR_ATK0 + i));
  }
}

void draw_attackers(void) {
  byte i;
  for (i = 0; i < MAX_ATTACKERS; i++) draw_attacker(i);
}

/* shoot2: dock when ydist==0 (byte wrap); else aim and y += 128 */
void return_attacker(AttackingEnemy* a) {
  byte fi = (byte)(a->findex - 1);
  byte destx = get_attacker_x(fi);
  byte desty = get_attacker_y(fi);
  byte ydist = (byte)(desty - PIX(a->y));
  if (ydist == 0) {
    formation[fi].shape = a->shape;
    a->findex = 0;
  } else {
    a->dir = (byte)((ydist + 16) & 31);
    a->x = (word)destx << 8;
    a->y += 128;
  }
}

/* shoot2: x += isin*2, y += icos*2; return when Y wraps to 0 */
void fly_attacker(AttackingEnemy* a) {
  a->x += isin(a->dir) * 2;
  a->y += icos(a->dir) * 2;
  if (PIX(a->y) == 0) a->returning = 1;
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

/* shoot2: y<128 or exploding → turn on x<112; else fire if missile slot free */
void think_attackers(void) {
  byte i;
  for (i = 0; i < MAX_ATTACKERS; i++) {
    AttackingEnemy* a = &attackers[i];
    byte x, y;
    if (!a->findex) continue;
    x = PIX(a->x);
    y = PIX(a->y);
    if (y < 128 || player_exploding) {
      if (x < 112) a->dir++;
      else a->dir--;
    } else if (!missiles[i].active) {
      /* shoot2: ypos=245-y, dy=-2 (HW Y inverted) → MCR: down at 2× speed */
      missiles[i].active = 1;
      missiles[i].x = SCR((word)(x + 8));
      missiles[i].y = SCR((word)y);
      missiles[i].dx = 0;
      missiles[i].dy = 4;
    }
  }
}

void formation_to_attacker(byte fi) {
  byte i;
  if (fi >= MAX_IN_FORMATION || !formation[fi].shape) return;
  for (i = 0; i < MAX_ATTACKERS; i++) {
    AttackingEnemy* a = &attackers[i];
    if (a->findex == 0) {
      a->x = (word)get_attacker_x(fi) << 8;
      a->y = (word)get_attacker_y(fi) << 8;
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
    if (missiles[i].dy < 0) {
      byte step = (byte)(-missiles[i].dy);
      if (missiles[i].y < step) { missiles[i].active = 0; continue; }
      missiles[i].y -= step;
    } else if (missiles[i].dy > 0) {
      missiles[i].y += (byte)missiles[i].dy;
      if (missiles[i].y > 470) missiles[i].active = 0;
    }
  }
}

void move_player(void) {
  if (LEFT1 && player_x > 16) player_x -= 2;
  if (RIGHT1 && player_x < 464) player_x += 2;
  if ((FIRE1 || START1) && !missiles[PLAYER_MISSILE].active) {
    missiles[PLAYER_MISSILE].active = 1;
    /* S_BULLET art is centered in 32×32 — same origin as ship centers the shot */
    missiles[PLAYER_MISSILE].x = player_x;
    missiles[PLAYER_MISSILE].y = PLAYER_Y - 16;
    missiles[PLAYER_MISSILE].dx = 0;
    missiles[PLAYER_MISSILE].dy = -8; /* shoot2 dy=4 ×2 */
  }
  if (!player_exploding)
    set_sprite_xy(SPR_PLAYER, S_PLAYER, 0, player_x, PLAYER_Y);
}

/* Galaxian shoot2: unsigned wrap — (x-x0) < w as unsigned */
char in_rect(byte x, byte y, byte x0, byte y0, byte w, byte h) {
  return ((byte)(x - x0) < w && (byte)(y - y0) < h);
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
  hide_sprite(SPR_BULLET);
}

void clear_all_missiles(void) {
  byte i;
  erase_enemy_missiles();
  for (i = 0; i < MAX_MISSILES; i++) missiles[i].active = 0;
  hide_sprite(SPR_BULLET);
}

/* Put divers back in their slots and redraw formation (restart wave). */
void recall_attackers(void) {
  byte i, r;
  for (i = 0; i < MAX_ATTACKERS; i++) {
    AttackingEnemy* a = &attackers[i];
    if (a->findex) {
      formation[(byte)(a->findex - 1)].shape = a->shape;
      a->findex = 0;
      a->returning = 0;
      hide_sprite((byte)(SPR_ATK0 + i));
    }
  }
  for (r = 0; r < ENEMY_ROWS; r++) draw_formation_row(r);
}

void new_player_ship(void) {
  player_exploding = 0;
  player_x = PLAYER_X0;
  clear_all_missiles();
  set_sprite_xy(SPR_PLAYER, S_PLAYER, 0, player_x, PLAYER_Y);
}

void does_player_shoot_formation(void) {
  byte mx, my, column, localx, index;
  signed char row;
  byte xoffset;
  if (!missiles[PLAYER_MISSILE].active) return;
  /* collide at bullet graphic center (art inset +16,+16 in 32×32) */
  mx = (byte)((missiles[PLAYER_MISSILE].x + 16) >> 1);
  my = (byte)((missiles[PLAYER_MISSILE].y + 16) >> 1);
  row = (signed char)((my - GAL_Y0) / GAL_YS);
  if (row < 0 || row >= ENEMY_ROWS) return;
  xoffset = (byte)(mx - GAL_X0 - (byte)formation_offset_x);
  column = (byte)(xoffset / GAL_XS);
  localx = (byte)(xoffset - column * GAL_XS);
  if (column < ENEMIES_PER_ROW && localx < GAL_HIT) {
    index = (byte)(column + (byte)row * ENEMIES_PER_ROW);
    if (formation[index].shape) {
      formation[index].shape = 0;
      enemies_left--;
      blowup_at(SCR(get_attacker_x(index)), SCR(get_attacker_y(index)));
      hide_player_missile();
      add_score(0x0002);
    }
  }
}

void does_player_shoot_attacker(void) {
  byte i, mx, my;
  if (!missiles[PLAYER_MISSILE].active) return;
  mx = (byte)((missiles[PLAYER_MISSILE].x + 16) >> 1);
  my = (byte)((missiles[PLAYER_MISSILE].y + 16) >> 1);
  for (i = 0; i < MAX_ATTACKERS; i++) {
    AttackingEnemy* a = &attackers[i];
    if (a->findex && in_rect(mx, my, PIX(a->x), PIX(a->y), GAL_HIT, GAL_HIT)) {
      blowup_at(SCR(PIX(a->x)), SCR(PIX(a->y)));
      a->findex = 0;
      enemies_left--;
      hide_player_missile();
      add_score(0x0005);
      break;
    }
  }
}

/* shoot2: enemy missile slots only; 16×16 in galaxian space */
void does_missile_hit_player(void) {
  byte i;
  byte px, py;
  if (player_exploding) return;
  px = (byte)(player_x >> 1);
  py = (byte)(PLAYER_Y >> 1);
  for (i = 0; i < MAX_ATTACKERS; i++) {
    if (missiles[i].active &&
        in_rect((byte)(missiles[i].x >> 1), (byte)(missiles[i].y >> 1),
                px, py, GAL_HIT, GAL_HIT)) {
      player_exploding = 1;
      clear_all_missiles();
      hide_sprite(SPR_PLAYER);
      set_sprite_xy(SPR_PLAYER, S_BOOM1, 1, player_x, PLAYER_Y);
      break;
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

void redraw_formation(void) {
  byte r;
  for (r = 0; r < ENEMY_ROWS; r++) draw_formation_row(r);
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
  enemy_exploding = 0;
  framecount = 0;
  redraw_formation();
  new_player_ship();

  while (end_timer) {
    wait_frame();
    framecount++;

    if (player_exploding) {
      /* shoot2: animate, then after >32 frames respawn if enemies remain */
      if ((framecount & 7) == 0) {
        set_sprite_xy(SPR_PLAYER,
                      (player_exploding & 1) ? S_BOOM1 : S_BOOM2, 1,
                      player_x, PLAYER_Y);
        player_exploding++;
        if (player_exploding > 32) {
          lives--;
          draw_score();
          if (lives && enemies_left) {
            recall_attackers();
            new_player_ship();
          } else {
            player_exploding = 0;
            hide_sprite(SPR_PLAYER);
            end_timer = 1;
          }
        }
      }
    } else {
      /* shoot2: every 128 frames while more than 8 remain */
      if ((framecount & 0x7f) == 0 && enemies_left > 8)
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
  put_string(11, 14, "GAME OVER", PAL_ORANGE);
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
    put_string(10, 8, "SOLARIAN", PAL_PINK);
    put_string(8, 10, "GALAXIAN PORT", PAL_CYAN);
    put_string(8, 14, "PRESS START", PAL_HUD);
    put_string(7, 16, "FIRE TO SHOOT", PAL_ORANGE);
    while (!(FIRE1 || START1)) wait_frame();
    while (FIRE1 || START1) wait_frame();
    play_round();
  }
}
