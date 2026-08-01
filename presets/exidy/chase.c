/*
 * Chase for Exidy UGB v2 — port of Shiru's NES Chase (presets/nes/chase).
 * Based on the Coleco/SG-1000 port (presets/coleco/chase.c).
 *
 * 14×N grid of 16×16 cells (2×2 tiles) on the 32×32 playfield.
 * Venture-style sprites: player on HW motion object 1; enemies are
 * softsprites (2×2 character tiles redrawn each frame).
 *
 * Controls: joystick move, Fire / Start = start / continue / pause.
 */
//#link "exidy.c"
#include "exidy.h"
#include "exidy_font.h"
#include "chase_gfx.h"

#define TILE_WTL    0x80
#define TILE_WTR    0x81
#define TILE_WBL    0x82
#define TILE_WBR    0x83
#define TILE_FLOOR  0x84
#define TILE_GTL    0xC0
#define TILE_GTR    0xC1
#define TILE_GBL    0xC2
#define TILE_GBR    0xC3
#define TILE_GTL2   0xC4
#define TILE_GTR2   0xC5
#define TILE_GBL2   0xC6
#define TILE_GBR2   0xC7
#define TILE_ETL    0xD0  /* enemy softsprite 2x2 */
#define TILE_ETR    0xD1
#define TILE_EBL    0xD2
#define TILE_EBR    0xD3

#define SPR_PLAYER  0
#define SPR_PLAYER2 1

#define MAP_W        14
#define MAP_H        13
#define MAP_X0       2
#define LEVELS_ALL   5
#define ACTORS_MAX   4

#define TILE_PX      16
#define FP_BITS      4
#define TILE_TO_POS(t)  ((word)(t) << (4 + FP_BITS))
#define POS_TO_TILE(p)  ((byte)((p) >> (4 + FP_BITS)))
#define POS_SNAP_MASK   0xff00

#define T_FLOOR  0
#define T_WALL   1
#define T_ITEM   2

#define DIR_NONE  0
#define DIR_LEFT  1
#define DIR_RIGHT 2
#define DIR_UP    4
#define DIR_DOWN  8

typedef struct {
  word x, y;
  word cnt;
  word speed;
  byte dir;
  byte wait;
  byte kind;
} Actor;

byte joy_left, joy_right, joy_up, joy_down, joy_fire;
byte fire_prev;

byte map[MAP_W * MAP_H];
Actor actors[ACTORS_MAX];
byte actor_n;
byte game_level;
byte game_lives;
byte items_count;
byte items_collected;
byte game_clear;
byte game_done;
byte game_paused;
byte spawn_wait;
word rnd = 0xCACE;
byte frame_cnt;
byte map_y0; /* tile row of map row 0 */
/* Softsprite screen-tile anchors (0xff = not drawn). Index matches actors[]. */
byte soft_sx[ACTORS_MAX];
byte soft_sy[ACTORS_MAX];

const byte level_h[LEVELS_ALL] = { 7, 9, 9, 11, 11 };

const char* const levels[LEVELS_ALL][MAP_H] = {
  {
  "##############","####P*****####","####*####*####","####******####",
  "####*####*####","####*****1####","##############","##############",
  "##############","##############","##############","##############","##############"
  },
  {
  "##############","###P***#**1###","###*##*#*#*###","###********###",
  "#####*#*#*####","###********###","###*#*#*##*###","###2*******###",
  "##############","##############","##############","##############","##############"
  },
  {
  "##############","###P***#**1###","###*##*#*#*###","#********#***#",
  "#*#*#*##*#*#*#","#***#********#","###*#*#*##*###","###***#***2###",
  "##############","##############","##############","##############","##############"
  },
  {
  "##############","###P***#######","###*##*#****1#","###*##*#*#*#*#",
  "#**********#*#","#*#*#*##*#*#*#","#*#**********#","#*#*#*#*##*###",
  "#2****#*##*###","#######***3###","##############","##############","##############"
  },
  {
  "##############","##P********1##","##*##*##*##*##","#************#",
  "#*#*###*#*#*##","#****#*******#","#******#*****#","#*#*#*###*#*##",
  "#************#","##2********3##","##############","##############","##############"
  }
};

byte rand8(void) {
  rnd = rnd * 17 + 53;
  return (byte)(rnd >> 8);
}

void read_controls(void) {
  joy_left  = LEFT1;
  joy_right = RIGHT1;
  joy_up    = UP1;
  joy_down  = DOWN1;
  joy_fire  = FIRE1 || START1;
}

byte map_at(byte x, byte y) {
  if (x >= MAP_W || y >= level_h[game_level]) return T_WALL;
  return map[y * MAP_W + x];
}

void map_set(byte x, byte y, byte t) {
  map[y * MAP_W + x] = t;
}

void put_digit(byte x, byte y, byte d) {
  put_char(x, y, (char)('0' + (d % 10)));
}

void draw_cell(byte x, byte y) {
  byte t = map_at(x, y);
  byte sx = (byte)(MAP_X0 + (x << 1));
  byte sy = (byte)(map_y0 + (y << 1));
  byte spark = (frame_cnt & 16) != 0;
  if (t == T_WALL) {
    poke_tile(sx, sy, TILE_WTL);
    poke_tile(sx + 1, sy, TILE_WTR);
    poke_tile(sx, sy + 1, TILE_WBL);
    poke_tile(sx + 1, sy + 1, TILE_WBR);
  } else if (t == T_ITEM) {
    if (spark) {
      poke_tile(sx, sy, TILE_GTL2);
      poke_tile(sx + 1, sy, TILE_GTR2);
      poke_tile(sx, sy + 1, TILE_GBL2);
      poke_tile(sx + 1, sy + 1, TILE_GBR2);
    } else {
      poke_tile(sx, sy, TILE_GTL);
      poke_tile(sx + 1, sy, TILE_GTR);
      poke_tile(sx, sy + 1, TILE_GBL);
      poke_tile(sx + 1, sy + 1, TILE_GBR);
    }
  } else {
    poke_tile(sx, sy, TILE_FLOOR);
    poke_tile(sx + 1, sy, TILE_FLOOR);
    poke_tile(sx, sy + 1, TILE_FLOOR);
    poke_tile(sx + 1, sy + 1, TILE_FLOOR);
  }
}

void draw_hud(void) {
  put_string(0, 0, "LV");
  put_digit(3, 0, game_level + 1);
  put_string(5, 0, "GEM");
  put_digit(9, 0, items_collected / 10);
  put_digit(10, 0, items_collected % 10);
  put_string(12, 0, "OF");
  put_digit(15, 0, items_count / 10);
  put_digit(16, 0, items_count % 10);
  put_string(18, 0, "HP");
  put_digit(21, 0, game_lives > 0 ? (byte)(game_lives - 1) : 0);
  if (game_paused) put_string(23, 0, "PAUSE");
  else put_string(23, 0, "     ");
}

byte can_enter(byte tx, byte ty) {
  return map_at(tx, ty) != T_WALL;
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
  a->cnt = (word)TILE_PX << FP_BITS;
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
}

void load_level(byte li) {
  byte x, y, h, i;
  const char* row;
  actor_n = 0;
  items_count = 0;
  items_collected = 0;
  h = level_h[li];
  map_y0 = (byte)(1 + ((30 - (h << 1)) >> 1));
  clrscr(' ');
  hide_sprites();
  for (i = 0; i < ACTORS_MAX; i++) {
    soft_sx[i] = 0xff;
    soft_sy[i] = 0xff;
  }
  for (y = 0; y < h; y++) {
    row = levels[li][y];
    for (x = 0; x < MAP_W; x++) {
      char c = row[x];
      byte t = T_FLOOR;
      if (c == '#') t = T_WALL;
      else if (c == '*') { t = T_ITEM; items_count++; }
      else if (c == 'P' || c == '1' || c == '2' || c == '3') {
        Actor* a = &actors[actor_n];
        a->x = TILE_TO_POS(x);
        a->y = TILE_TO_POS(y);
        a->cnt = 0;
        a->dir = DIR_NONE;
        if (c == 'P') { a->kind = 0; a->speed = 32; a->wait = 16; }
        else {
          a->kind = (byte)(c - '0');
          a->speed = (word)(10 + ((a->kind - 1) << 1));
          a->wait = (byte)(16 + (a->kind << 4));
        }
        actor_n++;
        t = T_FLOOR;
      }
      map[y * MAP_W + x] = t;
    }
  }
  for (y = 0; y < h; y++)
    for (x = 0; x < MAP_W; x++)
      draw_cell(x, y);
  spawn_wait = (byte)(actor_n << 4);
}

/* Restore map cells under a 2×2 softsprite at screen tiles (sx,sy). */
void undraw_soft(byte sx, byte sy) {
  byte dx, dy;
  if (sx == 0xff) return;
  for (dy = 0; dy < 2; dy++) {
    for (dx = 0; dx < 2; dx++) {
      byte tx = (byte)(sx + dx);
      byte ty = (byte)(sy + dy);
      byte mx, my;
      if (tx < MAP_X0 || ty < map_y0) continue;
      mx = (byte)((tx - MAP_X0) >> 1);
      my = (byte)((ty - map_y0) >> 1);
      if (mx < MAP_W && my < level_h[game_level])
        draw_cell(mx, my);
    }
  }
}

void draw_soft_enemy(byte sx, byte sy) {
  poke_tile(sx, sy, TILE_ETL);
  poke_tile(sx + 1, sy, TILE_ETR);
  poke_tile(sx, sy + 1, TILE_EBL);
  poke_tile(sx + 1, sy + 1, TILE_EBR);
}

void draw_actors(void) {
  byte i;
  byte px, py, shape, sx, sy;
  Actor* a;

  /* Erase previous enemy softsprites first (may overlap). */
  for (i = 1; i < actor_n; i++) {
    undraw_soft(soft_sx[i], soft_sy[i]);
    soft_sx[i] = 0xff;
    soft_sy[i] = 0xff;
  }

  /* Player on HW motion object 1 */
  a = &actors[0];
  px = (byte)(MAP_X0 * 8 + (a->x >> FP_BITS));
  py = (byte)(map_y0 * 8 + (a->y >> FP_BITS));
  shape = (frame_cnt & 8) ? SPR_PLAYER2 : SPR_PLAYER;
  if (a->wait && (a->wait >= 16 || (a->wait & 2)))
    set_sprite(0, shape, 0xff, 0xff);
  else
    set_sprite_screen(0, shape, px, py);
  set_sprite(1, 0, 0xff, 0xff); /* MO2 unused */

  /* Enemies as Venture-style softsprites (8px-quantized 2×2 tiles). */
  for (i = 1; i < actor_n; i++) {
    a = &actors[i];
    if (a->wait && (a->wait >= 16 || (a->wait & 2))) continue;
    px = (byte)(MAP_X0 * 8 + (a->x >> FP_BITS));
    py = (byte)(map_y0 * 8 + (a->y >> FP_BITS));
    sx = px >> 3;
    sy = py >> 3;
    if (sx > 30 || sy > 30) continue;
    draw_soft_enemy(sx, sy);
    soft_sx[i] = sx;
    soft_sy[i] = sy;
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
    if (!((px + 4) >= (ex + 12) || (ex + 4) >= (px + 12) ||
          (py + 4) >= (ey + 12) || (ey + 4) >= (py + 12)))
      return 1;
  }
  return 0;
}

void animate_gems(void) {
  byte x, y, h;
  if ((frame_cnt & 15) != 0) return;
  h = level_h[game_level];
  for (y = 0; y < h; y++)
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
  byte pick;
  if (prev != DIR_RIGHT && can_enter((byte)(tx - 1), ty)) dirs[n++] = DIR_LEFT;
  if (prev != DIR_LEFT  && can_enter((byte)(tx + 1), ty)) dirs[n++] = DIR_RIGHT;
  if (prev != DIR_DOWN  && can_enter(tx, (byte)(ty - 1))) dirs[n++] = DIR_UP;
  if (prev != DIR_UP    && can_enter(tx, (byte)(ty + 1))) dirs[n++] = DIR_DOWN;
  if (!n) return;
  pick = dirs[rand8() % n];
  actor_try_dir(id, pick);
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
    a->x &= POS_SNAP_MASK;
    a->y &= POS_SNAP_MASK;
    try_collect(id);
  }
}

void wait_frames(byte n) {
  while (n--) wait_vblank();
}

void wait_for_fire(void) {
  read_controls();
  while (!joy_fire) { wait_vblank(); read_controls(); }
  while (joy_fire) { wait_vblank(); read_controls(); }
}

void title_screen(void) {
  clrscr(' ');
  hide_sprites();
  put_string(13, 8, "CHASE");
  put_string(7, 12, "COLLECT ALL GEMS");
  put_string(9, 14, "AVOID ENEMIES");
  put_string(9, 18, "PRESS FIRE");
  put_string(8, 22, "PD SHIRU 2012");
  wait_for_fire();
  wait_frames(20);
}

void show_level_banner(void) {
  clrscr(' ');
  hide_sprites();
  put_string(12, 11, "LEVEL");
  put_digit(18, 11, game_level + 1);
  wait_frames(50);
}

void show_game_over(void) {
  clrscr(' ');
  hide_sprites();
  put_string(11, 11, "GAME OVER");
  wait_for_fire();
}

void show_well_done(void) {
  clrscr(' ');
  hide_sprites();
  put_string(11, 10, "WELL DONE");
  put_string(6, 13, "ALL GEMS COLLECTED");
  wait_for_fire();
}

void game_loop(void) {
  byte i;
  hide_sprites();
  load_level(game_level);
  draw_hud();
  game_done = 0;
  game_clear = 0;
  game_paused = 0;
  frame_cnt = 0;
  fire_prev = 1;
  while (!game_done) {
    wait_vblank();
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
      game_done = 1;
      wait_frames(100);
    }
  }
  if (game_clear) wait_frames(80);
  hide_sprites();
}

void setup_graphics(void) {
  load_charset((const byte*)FONT8X8, sizeof(FONT8X8));
  /* Walls/floor at 0x80 (char color bank 6); gems/enemy at 0xC0 (bank 7). */
  memcpy(CHAR_RAM + 0x80 * 8, chase_chars, 5 * 8);
  memcpy(CHAR_RAM + 0xC0 * 8, chase_chars + 8 * 8, 8 * 8);
  memcpy(CHAR_RAM + TILE_ETL * 8, chase_enemy_chars, 4 * 8);
  /* Bright playfield: cyan walls / yellow gems / white text-ish. */
  set_palette(0b10111100, 0b11111010, 0b01000001);
}

void main(void) {
  exidy_init();
  setup_graphics();
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
