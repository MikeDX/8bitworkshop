/*
 * Pac-Man — C clone on Pac-Man hardware (Namco gfx + ROM sound driver).
 *
 * Regenerate assets/sound: python3 scripts/gen_pacman_assets.py
 * Sound engine is relocatable ASM in pacman_sound.c (VBLANK hook).
 *
 * Controls: D-pad move, Start from title / after death.
 */
//#link "pacman_common.c"
//#link "pacman_assets.c"
//#link "pacman_sound.c"
//#link "pacman_sfx.c"
//#link "pacman_maze.c"
//#link "pacman_actors.c"
//#link "pacman_render.c"

/* Keep _CODE under 0x4000 (tile_rom) — IDE SDCC disables opts without this.
 * Size over speed: title/attract/HUD dominate this file; gameplay hot paths
 * live in pacman_actors.c / pacman_render.c. */
#pragma opt_code_size

#include "pacman_common.h"
#include "pacman_assets.h"
#include "pacman_game.h"
#include "pacman_sfx.h"
#include "pacman_maze.h"
#include "pacman_actors.h"

/* ---- globals ---- */
word rnd;
word score;
word hiscore;
word power_ticks;
word anim_ticks;
word round_ticks;
word fright_freq;
word fruit_ticks;
word fruit_visible;
word force_house;
word freeze_ticks;
word pac_x, pac_y;
word pac_frac;

byte lives;
byte level;
byte credits; /* 0..99 — attract / start gate */
byte dots_left;
byte dots_eaten;
byte game_over;
byte waka;
byte fright_on;
byte fright_tick;
byte eat_combo;
byte elroy;
byte elroy_suspended;
byte freeze_ghost;
byte freeze_score;
byte pac_dir, pac_want;
byte pac_dead;
byte tick;
byte pac_stop;
byte global_dot_mode;
byte global_dot_counter;
byte attract_demo;
byte eyes_present;

Ghost ghosts[GHOST_N];

extern const byte ghost_pal[GHOST_N];

static word hud_score;
static word hud_hiscore;
static byte hud_ready; /* 0 = force labels */

/* Arcade energizer tile positions (screen coords). */
static const byte pill_x[4] = { 1, 26, 1, 26 };
static const byte pill_y[4] = { 6, 6, 26, 26 };

/*
 * Pixel (py, px) → tile: ty = py/8, tx = 27 - px/8
 *   1UP         (0,192) → (3,0)
 *   HIGH SCORE  (0,144) → (9,0)
 *   2UP         (0,40)  → (22,0)
 *   PLAYER ONE  (112,144) → (9,14)
 *   READY!      (160,128) → (11,20)
 *   chase pill  (160,184) → (4,20)
 *   score pill  (208,136) → (10,26)
 */

/* Write tile only — top HUD rows keep pal 0xF (1UP flash flips pal alone). */
static void poke_tile_only(byte x, byte y, byte tile) {
  if (x >= 28 || y >= 36) return;
  *((byte*)(0x4000 + vram_addr(x, y))) = tile;
}

/* Right-aligned score ending at (x,y); at least "00". Tile updates only. */
static void draw_score_r(byte x, byte y, word n) {
  byte d0, d1, d2, d3, d4;
  word v = n;
  byte pos;

  d0 = 0; while (v >= 10000) { v = (word)(v - 10000); d0++; }
  d1 = 0; while (v >= 1000) { v = (word)(v - 1000); d1++; }
  d2 = 0; while (v >= 100) { v = (word)(v - 100); d2++; }
  d3 = 0; while (v >= 10) { v = (word)(v - 10); d3++; }
  d4 = (byte)v;

  pos = x;
  poke_tile_only(pos, y, (byte)('0'));
  pos--;
  poke_tile_only(pos, y, (byte)('0' + d4));
  if (n >= 10) { pos--; poke_tile_only(pos, y, (byte)('0' + d3)); }
  if (n >= 100) { pos--; poke_tile_only(pos, y, (byte)('0' + d2)); }
  if (n >= 1000) { pos--; poke_tile_only(pos, y, (byte)('0' + d1)); }
  if (n >= 10000) { pos--; poke_tile_only(pos, y, (byte)('0' + d0)); }
}

/* Active-player "1UP" blinks via color RAM only (arcade). */
static byte flash_1up_pal; /* 0 = unset; set on first flash / HUD force */

static void flash_1up(void) {
  byte pal = (anim_ticks & 0x10) ? 0x0F : 0;
  if (pal == flash_1up_pal) return;
  flash_1up_pal = pal;
  poke_pal(3, 0, pal);
  poke_pal(4, 0, pal);
  poke_pal(5, 0, pal);
}

/* Energizer blink (pal 0x10 / 0). Solid only during READY banner. */
static byte flash_pill_pal;

static void flash_power_pills(byte blinking) {
  byte i, pal;
  pal = blinking ? ((anim_ticks & 0x8) ? 0x10 : 0) : 0x10;
  if (pal == flash_pill_pal) return;
  flash_pill_pal = pal;
  for (i = 0; i < 4; i++)
    poke_pal(pill_x[i], pill_y[i], pal);
}

void draw_hud(void) {
  byte force = (byte)(hud_ready == 0);
  byte i;

  if (score > hiscore) hiscore = score;

  if (!force && score == hud_score && hiscore == hud_hiscore)
    return;

  if (force) {
    /* Entire top two rows: tiles + pal 0xF once; later frames only change tiles. */
    for (i = 0; i < 28; i++) {
      poke_tile(i, 0, T_BLANK, 0x0F);
      poke_tile(i, 1, T_BLANK, 0x0F);
    }
    put_string(3, 0, "1UP", 0x0F);
    put_string(9, 0, "HIGH SCORE", 0x0F);
    put_string(22, 0, "2UP", 0x0F);
    flash_1up_pal = 0x0F;
  }

  if (force || score != hud_score) {
    draw_score_r(6, 1, score);
    hud_score = score;
  }
  if (force || hiscore != hud_hiscore) {
    draw_score_r(16, 1, hiscore);
    hud_hiscore = hiscore;
  }

  hud_ready = 1;
}

/*
 * New game: prelude with PLAYER ONE + READY!, sprites hidden for first half
 * (~2s), then clear PLAYER ONE and reveal actors for READY phase (~2s).
 * After death / next level: READY! with actors visible only.
 */
static void show_ready_banner(byte player_one) {
  byte t;

  flash_power_pills(0); /* solid during READY (not blinking) */
  put_string(11, 20, "READY!", 9);
  if (player_one) {
    put_string(9, 14, "PLAYER ONE", 5);
    hide_all_sprites();
    play_prelude();
    for (t = 0; t < 120; t++) {
      wait_vblank();
      watchdog = 0;
      anim_ticks++;
      flash_1up();
    }
    put_string(9, 14, "          ", 0);
  }

  for (t = 0; t < 130; t++) {
    wait_vblank();
    watchdog = 0;
    anim_ticks++;
    flash_1up();
    actors_draw_anim(0);
  }
  put_string(11, 20, "      ", 0);
}

/*
 * Attract / intro (floooh intro_tick timing) + chase demo.
 * Coin adds a credit (cap 99), plays ding, and aborts the slow intro so
 * START can begin a game once credits > 0.
 */
#define TILE_PTS0        0x5D
#define TILE_PTS1        0x5E
#define TILE_PTS2        0x5F
#define TILE_COPYRIGHT   0x5C  /* (C) */
#define TILE_PERIOD      0x25  /* . */

/* Soft ghost: 2×3 tile stamp (sprites free for chase later). */
static void attract_draw_ghost(byte x, byte y, byte pal) {
  poke_tile(x, y,                         0xB0, pal);
  poke_tile((byte)(x + 1), y,             0xB1, pal);
  poke_tile(x, (byte)(y + 1),             0xB2, pal);
  poke_tile((byte)(x + 1), (byte)(y + 1), 0xB3, pal);
  poke_tile(x, (byte)(y + 2),             0xB4, pal);
  poke_tile((byte)(x + 1), (byte)(y + 2), 0xB5, pal);
}

/* Attract energizers — arcade pixel (py,px) → tile via comment above.
 *   chase pill  (160,184) → (4,20)
 *   score legend (208,136) → (10,26)  drawn at t==570 with "50 PTS"
 */
#define ATTRACT_CHASE_PX  4
#define ATTRACT_CHASE_PY  20
#define ATTRACT_SCORE_PX  10
#define ATTRACT_SCORE_PY  26

/* "CREDIT  0" / "CREDIT 10" — arcade spacing (two spaces if <10, one if ≥10). */
static void draw_credits(void) {
  byte n = credits;
  put_string(3, 35, "CREDIT", 0x0F);
  if (n > 99) n = 99;
  if (n < 10) {
    poke_tile(9, 35, T_BLANK, 0x0F);
    poke_tile(10, 35, T_BLANK, 0x0F);
    poke_tile(11, 35, (byte)('0' + n), 0x0F);
  } else {
    byte tens = 0;
    while (n >= 10) { n = (byte)(n - 10); tens++; }
    poke_tile(9, 35, T_BLANK, 0x0F);
    poke_tile(10, 35, (byte)('0' + tens), 0x0F);
    poke_tile(11, 35, (byte)('0' + n), 0x0F);
  }
}

/* Edge-detect coin → credit + ding. Returns 1 if a credit was added. */
static byte coin_was_down;

static byte poll_credit(void) {
  byte down = COIN1 ? 1 : 0;
  byte added = 0;
  if (down && !coin_was_down) {
    if (credits < 99) {
      credits++;
      play_sfx(5);
      draw_credits();
      added = 1;
    }
  }
  coin_was_down = down;
  return added;
}

static void attract_draw_copyright(void) {
  /* (C) 1980 MIDWAY MFG.CO. — arcade tile (4,28), pal 3 */
  poke_tile(4, 31, TILE_COPYRIGHT, 3);
  poke_tile(5, 31, T_BLANK, 3);
  put_string(6, 31, "1981 DX AUTOMATICS", 3);

  // put_string(6, 28, "1980 MIDWAY MFG", 3);
  // poke_tile(21, 28, TILE_PERIOD, 3);
  // put_string(22, 28, "CO", 3);
  // poke_tile(24, 28, TILE_PERIOD, 3);
}

/* Fixed-width rows in CODE — `[][]` / pointer tables are invalid or hit _INITIALIZED. */
#if defined(ENABLE_ZOMBIE)
static const char ghost_names[6][9] = {
  "-SHADOW", "-SPEEDY", "-BASHFUL", "-POKEY", "-IDIOT", "-ZOMBIE"
};
static const char ghost_nicks[6][9] = {
  "\"BLINKY\"", "\"PINKY\"", "\"INKY\"", "\"CLYDE\"", "\"CURLY\"", "\"FRED\""
};
#elif defined(ENABLE_CURLY)
static const char ghost_names[5][9] = {
  "-SHADOW", "-SPEEDY", "-BASHFUL", "-POKEY", "-IDIOT"
};
static const char ghost_nicks[5][9] = {
  "\"BLINKY\"", "\"PINKY\"", "\"INKY\"", "\"CLYDE\"", "\"CURLY\""
};
#else
static const char ghost_names[4][9] = {
  "-SHADOW", "-SPEEDY", "-BASHFUL", "-POKEY"
};
static const char ghost_nicks[4][9] = {
  "\"BLINKY\"", "\"PINKY\"", "\"INKY\"", "\"CLYDE\""
};
#endif

/* Intro text ends; short hold, then chase. Pills flash only in chase. */
#if defined(ENABLE_ZOMBIE)
#define ATTRACT_PTS_T      810
#define ATTRACT_COPY_T     900
#define ATTRACT_CHASE_T0   960
#elif defined(ENABLE_CURLY)
#define ATTRACT_PTS_T      690
#define ATTRACT_COPY_T     780
#define ATTRACT_CHASE_T0   840
#else
#define ATTRACT_PTS_T      570
#define ATTRACT_COPY_T     660
#define ATTRACT_CHASE_T0   720
#endif
#define ATTRACT_ROW_CY     (20 * 8 + 4)
/* Pixel centers ≥224 are past the right edge (sprite hardware wraps). */
#define ATTRACT_SPAWN_X    240
#define ATTRACT_GHOST_GAP  16  /* frames between Pac / ghost spawns */
#define ATTRACT_PAL_BLACK  0   /* unused all-black sprite palette */

static void attract_blank_offscreen(byte spawned) {
  byte i;
  /* Pac / ghosts past the seam: black pal (no wrap flash). */
  if (pac_x < 8 || pac_x >= 224)
    ((byte*)0x4ff0)[1] = ATTRACT_PAL_BLACK;
  for (i = 0; i < GHOST_N; i++) {
    Ghost* g = &ghosts[i];
    if (!(spawned & (1 << i))) {
      hide_sprite((byte)(i + 1));
      continue;
    }
    g->y = ATTRACT_ROW_CY;
    if (g->x < 8 || g->x >= 224)
      ((byte*)0x4ff0)[(i + 1) * 2 + 1] = ATTRACT_PAL_BLACK;
  }
  hide_sprite(SPR_FRUIT); /* no fruit on attract */
}

/* Horizontal-only step — no tunnel wrap. Enter from x≥224; park after exit. */
static void attract_move_ghosts(byte spawned) {
  byte i, s, steps;
  Ghost* g;

  for (i = 0; i < GHOST_N; i++) {
    if (!(spawned & (1 << i))) continue;
    g = &ghosts[i];
    /* Parked after leaving the visible area. */
    if (g->dir == DIR_RIGHT && g->x >= 224) continue;
    if (g->dir == DIR_LEFT && g->x < 8) continue;
    steps = take_steps(&g->frac, ghost_speed_cached(g));
    for (s = 0; s < steps; s++) {
      if (g->dir == DIR_LEFT) {
        if (g->x > 0) g->x--;
      } else if (g->dir == DIR_RIGHT) {
        if (g->x < 255) g->x++;
      }
    }
    g->y = ATTRACT_ROW_CY;
  }
}

/*
 * Pac enters from the right → energizer → turn → eat ghosts.
 * Ends when the last eaten-ghost score popup finishes (maze attract next).
 */
static void attract_chase(void) {
  byte i;
  byte spawned = 0;
  byte eaten = 0;
  byte hunt = 0;
  byte was_power = 0;
  byte pill_pal = 0xFF;
  word chase_t = 0;

  attract_demo = 1;
  level = 0;
  dots_left = 244;
  dots_eaten = 0;
  score = 0;
  hud_ready = 0;
  draw_hud();

  actors_reset_level(0);
  /* Pac spawns at chase_t==0; ghosts follow on ATTRACT_GHOST_GAP beats — all at 240. */
  pac_x = ATTRACT_SPAWN_X;
  pac_y = ATTRACT_ROW_CY;
  pac_dir = DIR_LEFT;
  pac_want = DIR_LEFT;

  for (i = 0; i < GHOST_N; i++) {
    ghosts[i].x = 0;
    ghosts[i].y = 0;
    ghosts[i].dir = DIR_LEFT;
    ghosts[i].next_dir = DIR_LEFT;
    ghosts[i].mode = MODE_CHASE;
    ghosts[i].frac = 0;
    ghosts[i].speed_sig = 0xff;
    ghosts[i].color = ghost_pal[i];
  }

  /* Solid pills until chase starts flashing below. */
  poke_pal(ATTRACT_SCORE_PX, ATTRACT_SCORE_PY, 0x10);
  poke_pal(ATTRACT_CHASE_PX, ATTRACT_CHASE_PY, 0x10);

  while (!(START1 && credits)) {
    wait_vblank();
    watchdog = 0;
    anim_ticks++;
    poll_credit();

    /* Flash pills only while the chase demo is running. */
    {
      byte pp = (byte)((anim_ticks & 0x8) ? 0x10 : 0);
      if (pp != pill_pal) {
        pill_pal = pp;
        poke_pal(ATTRACT_SCORE_PX, ATTRACT_SCORE_PY, pp);
        if (peek_tile(ATTRACT_CHASE_PX, ATTRACT_CHASE_PY) == T_POWER_A)
          poke_pal(ATTRACT_CHASE_PX, ATTRACT_CHASE_PY, pp);
      }
    }

    /* Ghosts spawn one-by-one at ATTRACT_SPAWN_X; only timing differs. */
    for (i = 0; i < GHOST_N; i++) {
      word t_spawn = (word)((word)(i + 1) << 4); /* ATTRACT_GHOST_GAP==16 */
      if (!(spawned & (1 << i)) && chase_t == t_spawn) {
        Ghost* g = &ghosts[i];
        g->x = ATTRACT_SPAWN_X;
        g->y = ATTRACT_ROW_CY;
        g->frac = 0;
        g->speed_sig = 0xff;
        g->dir = g->next_dir = (hunt || power_ticks) ? DIR_RIGHT : DIR_LEFT;
        g->mode = (hunt || power_ticks) ? MODE_FRIGHT : MODE_CHASE;
        if (hunt || power_ticks) g->frightened = 1;
        spawned = (byte)(spawned | (1 << i));
      }
    }

    if (freeze_ticks) {
      update_ambient();
      actors_draw();
      attract_blank_offscreen(spawned);
      freeze_ticks--;
      /* fright timer paused during score popup (same as gameplay) */
      /* Last ghost score popup just finished → end title chase. */
      eaten = 0;
      for (i = 0; i < GHOST_N; i++) {
        if ((spawned & (1 << i)) && ghosts[i].mode == MODE_EYES)
          eaten++;
      }
      if (eaten == GHOST_N && !freeze_ticks)
        break;
      if (chase_t != 0xFFFF) chase_t++;
      continue;
    }

    pac_want = hunt ? DIR_RIGHT : DIR_LEFT;

    /* Keep fright/chase mode in sync for speed + draw. */
    for (i = 0; i < GHOST_N; i++) {
      Ghost* g;
      byte d;
      if (!(spawned & (1 << i))) continue;
      g = &ghosts[i];
      if (g->mode == MODE_EYES) {
        g->dir = DIR_RIGHT;
        continue;
      }
      d = hunt ? DIR_RIGHT : DIR_LEFT;
      g->dir = d;
      if (power_ticks) {
        g->mode = MODE_FRIGHT;
        g->frightened = 1;
      } else {
        g->frightened = 0;
        g->mode = hunt ? MODE_SCATTER : MODE_CHASE;
      }
    }

    pac_update();

    /* Energizer just eaten → reverse Pac + ghosts (arcade fright reverse). */
    if (power_ticks && !was_power) {
      hunt = 1;
      pac_dir = pac_want = DIR_RIGHT;
      for (i = 0; i < GHOST_N; i++) {
        Ghost* g;
        if (!(spawned & (1 << i))) continue;
        g = &ghosts[i];
        if (g->mode == MODE_EYES) continue;
        g->dir = g->next_dir = DIR_RIGHT;
        g->mode = MODE_FRIGHT;
        g->frightened = 1;
        g->speed_sig = 0xff;
      }
    }
    was_power = power_ticks ? 1 : 0;

    attract_move_ghosts(spawned);

    if (check_ghost_hits()) {
      /* Contact before pill — abort demo (spacing should prevent this). */
      break;
    }

    if (power_ticks) power_ticks--;
    update_ambient();
    actors_draw();
    attract_blank_offscreen(spawned);

    if (chase_t > 1200) break;
    if (chase_t != 0xFFFF) chase_t++;
  }

  attract_demo = 0;
  hide_all_sprites();
  sfx_off();
  power_ticks = 0;
  freeze_ticks = 0;
}

static void attract_draw_pts_legend(void) {
  poke_tile(10, 24, T_DOT_A, PAL_DOT);
  put_string(12, 24, "10 ", 0x0F);
  poke_tile(15, 24, TILE_PTS0, 0x0F);
  poke_tile(16, 24, TILE_PTS1, 0x0F);
  poke_tile(17, 24, TILE_PTS2, 0x0F);
  poke_tile(ATTRACT_SCORE_PX, ATTRACT_SCORE_PY, T_POWER_A, PAL_DOT);
  put_string(12, 26, "50 ", 0x0F);
  poke_tile(15, 26, TILE_PTS0, 0x0F);
  poke_tile(16, 26, TILE_PTS1, 0x0F);
  poke_tile(17, 26, TILE_PTS2, 0x0F);
}

void title_screen(void) {
  word t;
  byte i, y, pal;
  byte chase_done = 0;

  clrscr(0);
  hide_all_sprites();
  sfx_off();
  attract_demo = 0;
  coin_was_down = COIN1 ? 1 : 0;

  /* Player score shows 00 on attract; keep hiscore. */
  score = 0;
  hud_ready = 0;
  draw_hud();
  put_string(7, 5, "CHARACTER / NICKNAME", 0x0F);
  draw_credits();

  t = 0;
  /* Intro + chase; START with credits leaves. */
  while (!(START1 && credits)) {
    wait_vblank();
    watchdog = 0;
    anim_ticks++;

    if (poll_credit()) {
      /* Skip remaining timed reveals — still wait for chase delay. */
      if (t < ATTRACT_CHASE_T0) {
        if (t < ATTRACT_PTS_T)
          attract_draw_pts_legend();
        attract_draw_copyright();
        poke_tile(ATTRACT_CHASE_PX, ATTRACT_CHASE_PY, T_POWER_A, PAL_DOT);
        poke_pal(ATTRACT_SCORE_PX, ATTRACT_SCORE_PY, 0x10);
        poke_pal(ATTRACT_CHASE_PX, ATTRACT_CHASE_PY, 0x10);
        t = ATTRACT_CHASE_T0;
      }
    }

    if (!chase_done && t < ATTRACT_CHASE_T0) {
      /* Ghost intros: 2×3 tiles, then name +1s, nick +0.5s. */
      for (i = 0; i < GHOST_N; i++) {
        word t_ghost = (word)(60 + (word)i * 120);
        word t_name = (word)(t_ghost + 60);
        word t_nick = (word)(t_name + 30);
        y = (byte)(6 + i * 3);
        pal = ghost_pal[i];
        if (t == t_ghost) attract_draw_ghost(4, y, pal);
        if (t == t_name)
          put_string(7, (byte)(y + 1), ghost_names[i], pal);
        if (t == t_nick)
          put_string(18, (byte)(y + 1), ghost_nicks[i], pal);
      }

      if (t == ATTRACT_PTS_T) {
        attract_draw_pts_legend();
        /* Solid until chase — no flash during character intro. */
        poke_pal(ATTRACT_SCORE_PX, ATTRACT_SCORE_PY, 0x10);
      }

      /* copyright + chase energizer */
      if (t == ATTRACT_COPY_T) {
        attract_draw_copyright();
        poke_tile(ATTRACT_CHASE_PX, ATTRACT_CHASE_PY, T_POWER_A, PAL_DOT);
        poke_pal(ATTRACT_CHASE_PX, ATTRACT_CHASE_PY, 0x10);
      }

      if (t != 0xFFFF) t++;
    } else if (!chase_done) {
      attract_chase();
      chase_done = 1;
      /* Demo over — placeholder for maze attract; wait for START. */
    } else {
      poll_credit();
    }
  }

  while (START1) {
    wait_vblank();
    watchdog = 0;
    poll_credit();
  }
  if (credits) credits--;
  attract_demo = 0;
  sfx_off();
}

void show_death(void) {
  byte t, frame;
  byte i;
  word timeout;
  for (i = 1; i < 8; i++) hide_sprite(i);
  play_sfx(4);
  for (t = 0; t < 88; t++) {
    frame = (byte)(SP_DEATH0 + (t >> 3));
    if (frame > SP_DEATH_LAST) frame = SP_DEATH_LAST;
    set_sprite_ex(0, frame, PAL_YELLOW, (byte)(pac_x - 8), (byte)(pac_y - 8), 0);
    wait_vblank();
    watchdog = 0;
    if (t == 72) CH3_E_NUM = 0x20;
  }
  hide_sprite(0);
  /* Wait for coda bits to clear — timeout if engine missed a frame. */
  timeout = 180;
  while ((CH3_E_NUM & 0x20) && timeout--) {
    wait_vblank();
    watchdog = 0;
  }
  wait_vblank();
  CH3_E_NUM = 0x20;
  timeout = 180;
  while ((CH3_E_NUM & 0x20) && timeout--) {
    wait_vblank();
    watchdog = 0;
  }
  sfx_off();
}

void show_game_over(void) {
  byte t;
  /* Pixel (160,144) → tile (9,20); arcade uses pal 1. */
  put_string(9, 20, "GAME  OVER", 1);
  for (t = 0; t < 180; t++) {
    wait_vblank();
    watchdog = 0;
  }
}

void start_round(byte player_one) {
  hide_all_sprites();
  draw_maze();
  count_dots();
  actors_reset_level(0);
  hud_ready = 0;
  draw_hud();
  sfx_off(); /* silent through READY; ambient starts when play resumes */
  show_ready_banner(player_one);
}

void next_level(void) {
  byte t;
  sfx_off();
  for (t = 0; t < 60; t++) {
    wait_vblank();
    watchdog = 0;
  }
  if (level < 255) level++;
  global_dot_mode = 0;
  global_dot_counter = 0;
  start_round(0);
}

void game_loop(void) {
  lives = 3;
  level = 0;
  score = 0;
  game_over = 0;
  global_dot_mode = 0;
  global_dot_counter = 0;
  clrscr(0);
  start_round(1);

  while (!game_over) {
    wait_vblank();
    watchdog = 0;
    anim_ticks++;
    tick++;

    if (freeze_ticks) {
      freeze_ticks--;
      /* [CONFIRM] fright timer paused while score popup freezes the game */
      update_ambient();
      draw_hud();
      flash_1up();
      flash_power_pills(1); /* keep blinking while ghost-eat freeze */
      actors_draw();
      continue;
    }

    pac_update();
    ghosts_update();

    if (check_ghost_hits()) {
      show_death();
      if (lives) lives--;
      if (!lives) {
        game_over = 1;
        break;
      }
      /* [CONFIRM] death → global 7/17/32; keep personal; Elroy off until Clyde */
      global_dot_mode = 1;
      global_dot_counter = 0;
      elroy_suspended = 1;
      actors_reset_level(1);
      draw_hud();
      sfx_off(); /* no chase noise through READY */
      show_ready_banner(0);
    }

    if (power_ticks) power_ticks--;
    update_ambient();
    draw_hud();
    flash_1up();
    flash_power_pills(1);
    actors_draw();

    if (!dots_left)
      next_level();
  }
}

void main(void) {
  sound_enable = 1;
  flip_screen = 0;
  watchdog = 0;
  video_framecount = 0;
  rnd = 0xCACE;
  sfx_off(); /* arms pac_vblank_hook + clears WSG */
  pac_irq_enable();

  while (1) {
    title_screen();
    game_loop();
    show_game_over();
  }
}
