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

/* Uncomment ONE for maze-only boot:
 *   ATTRACT_DEV    — playback attract_path[] (silent)
 *   ATTRACT_RECORD — joystick training; logs DemoKeys @ 0x4CB0
 */
// #define ATTRACT_DEV
#define ATTRACT_RECORD

#if defined(ATTRACT_RECORD) && defined(ATTRACT_DEV)
#error "Define ATTRACT_RECORD or ATTRACT_DEV, not both"
#endif
#if defined(ATTRACT_DEV) || defined(ATTRACT_RECORD)
#define ATTRACT_MAZE_ONLY 1  /* drop title/chase/game_loop from _CODE */
#endif

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
byte pac_tx, pac_ty, pac_ox, pac_oy;
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
byte attract_corridor;
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

#ifndef ATTRACT_MAZE_ONLY
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
#define ATTRACT_ROW_TY     20
#define ATTRACT_ROW_OY     4
/* Pixel centers ≥224 are past the right edge (sprite hardware wraps).
 * Stored as tile+offset: 240 → tx=30, ox=0. */
#define ATTRACT_SPAWN_TX   30
#define ATTRACT_SPAWN_OX   0
#define ATTRACT_GHOST_GAP  16  /* frames between Pac / ghost spawns */
#define ATTRACT_PAL_BLACK  0   /* unused all-black sprite palette */

static void attract_blank_offscreen(byte spawned) {
  byte i;
  /* Pac / ghosts past the seam: black pal (no wrap flash). */
  if (pac_tx < 1 || pac_tx >= 28)
    ((byte*)0x4ff0)[1] = ATTRACT_PAL_BLACK;
  for (i = 0; i < GHOST_N; i++) {
    Ghost* g = &ghosts[i];
    if (!(spawned & (1 << i))) {
      hide_sprite((byte)(i + 1));
      continue;
    }
    g->ty = ATTRACT_ROW_TY; g->oy = ATTRACT_ROW_OY;
    if (g->tx < 1 || g->tx >= 28)
      ((byte*)0x4ff0)[(i + 1) * 2 + 1] = ATTRACT_PAL_BLACK;
  }
  hide_sprite(SPR_FRUIT); /* no fruit on attract */
}

/* Horizontal-only step — no tunnel wrap. Enter from tx≥28; park after exit. */
static void attract_move_ghosts(byte spawned) {
  byte i, s, steps;
  Ghost* g;

  for (i = 0; i < GHOST_N; i++) {
    if (!(spawned & (1 << i))) continue;
    g = &ghosts[i];
    /* Parked after leaving the visible area. */
    if (g->dir == DIR_RIGHT && g->tx >= 28) continue;
    if (g->dir == DIR_LEFT && g->tx < 1) continue;
    steps = take_steps(&g->frac, ghost_speed_cached(g));
    for (s = 0; s < steps; s++) {
      if (g->dir == DIR_LEFT) {
        if (g->ox) g->ox--;
        else if (g->tx) { g->tx--; g->ox = 7; }
      } else if (g->dir == DIR_RIGHT) {
        g->ox++;
        if (g->ox >= 8) { g->ox = 0; g->tx++; }
      }
    }
    g->ty = ATTRACT_ROW_TY; g->oy = ATTRACT_ROW_OY;
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
  attract_corridor = 1;
  level = 0;
  dots_left = 244;
  dots_eaten = 0;
  score = 0;
  hud_ready = 0;
  draw_hud();

  actors_reset_level(0);
  /* Pac spawns at chase_t==0; ghosts follow on ATTRACT_GHOST_GAP beats — all at 240. */
  pac_tx = ATTRACT_SPAWN_TX; pac_ox = ATTRACT_SPAWN_OX;
  pac_ty = ATTRACT_ROW_TY; pac_oy = ATTRACT_ROW_OY;
  pac_dir = DIR_LEFT;
  pac_want = DIR_LEFT;

  for (i = 0; i < GHOST_N; i++) {
    ghosts[i].tx = 0; ghosts[i].ox = 0;
    ghosts[i].ty = 0; ghosts[i].oy = 0;
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

    /* Ghosts spawn one-by-one at ATTRACT_SPAWN_TX; only timing differs. */
    for (i = 0; i < GHOST_N; i++) {
      word t_spawn = (word)((word)(i + 1) << 4); /* ATTRACT_GHOST_GAP==16 */
      if (!(spawned & (1 << i)) && chase_t == t_spawn) {
        Ghost* g = &ghosts[i];
        g->tx = ATTRACT_SPAWN_TX; g->ox = ATTRACT_SPAWN_OX;
        g->ty = ATTRACT_ROW_TY; g->oy = ATTRACT_ROW_OY;
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

  attract_corridor = 0;
  attract_demo = 0;
  hide_all_sprites();
  sfx_off();
  power_ticks = 0;
  freeze_ticks = 0;
}
#endif /* !ATTRACT_MAZE_ONLY */

/*
 * Maze attract: silent, no PLAYER/READY.
 *
 * ATTRACT_DEV: timed fake-stick from attract_path[].
 * ATTRACT_RECORD: live stick; log dir *changes* as DemoKey @ 0x4CB0
 *   (timer resets at maze start; first key is always {0, DIR_LEFT}).
 *
 * Playback entries: hold `.dir` from `.when` until the next entry.
 * Straights omitted. DEMO_REP warps to ATTRACT_PATH_LOOP.
 */
#define DEMO_END   0xFFFF
#define DEMO_REP   0xFE   /* not a DIR_*; .when = frame to wrap */

typedef struct {
  word when;
  byte dir;
} DemoKey;

/* ---- ATTRACT_RECORD: fixed RAM so you can dump it from the emulator ----
 * Layout @ 0x4CB0 (after _DATA ~0x4CAA, before sound @ 0x4E8C):
 *   DemoKey demo_rec[DEMO_REC_MAX]   — 3 bytes each (when, dir)
 *   byte    demo_rec_n               — entry count (incl. initial LEFT)
 * Dump e.g. MAME:  dump 4CB0,260
 */
#ifdef ATTRACT_RECORD
#define DEMO_REC_BASE  0x4CB0
#define DEMO_REC_MAX   128           /* 128*3 = 384 → ends 0x4E30 */

static DemoKey __at(DEMO_REC_BASE) demo_rec[DEMO_REC_MAX];
static byte __at(DEMO_REC_BASE + DEMO_REC_MAX * 3) demo_rec_n;
static byte demo_rec_last;

static word demo_t;

static void demo_rec_begin(void) {
  demo_t = 0;
  demo_rec_n = 0;
  demo_rec_last = DIR_LEFT;
  pac_dir = DIR_LEFT;
  pac_want = DIR_LEFT;
  demo_rec[0].when = 0;
  demo_rec[0].dir = DIR_LEFT;
  demo_rec_n = 1;
}

/* Edge-trigger on stick: record only when held dir changes. */
static void demo_rec_sample(void) {
  byte d = DIR_NONE;
  if (LEFT1) d = DIR_LEFT;
  if (RIGHT1) d = DIR_RIGHT;
  if (UP1) d = DIR_UP;
  if (DOWN1) d = DIR_DOWN;

  if (d == DIR_NONE)
    return; /* keep pac_want / last recorded */

  pac_want = d;
  if (d == demo_rec_last)
    return;
  demo_rec_last = d;
  if (demo_rec_n >= DEMO_REC_MAX)
    return;
  demo_rec[demo_rec_n].when = demo_t;
  demo_rec[demo_rec_n].dir = d;
  demo_rec_n++;
}

static void put_u8_dec3(byte x, byte y, byte n, byte pal) {
  byte h = 0, t = 0;
  while (n >= 100) { n = (byte)(n - 100); h++; }
  while (n >= 10) { n = (byte)(n - 10); t++; }
  put_digit(x, y, h, pal);
  put_digit((byte)(x + 1), y, t, pal);
  put_digit((byte)(x + 2), y, n, pal);
}

static void demo_rec_finish(void) {
  /* Optional end marker if room — count in demo_rec_n stays real keys. */
  if (demo_rec_n < DEMO_REC_MAX) {
    demo_rec[demo_rec_n].when = DEMO_END;
    demo_rec[demo_rec_n].dir = 0;
  }
  put_string(7, 16, "REC", 0x0F);
  put_u8_dec3(11, 16, demo_rec_n, 0x0F);
  put_string(7, 18, "RAM 4CB0", 0x0F);
}
#endif /* ATTRACT_RECORD */

#ifndef ATTRACT_RECORD
/* First key of the repeating block (DIR_LEFT after tunnel outro). */
#define ATTRACT_PATH_LOOP  34

/* Placeholder times every 40f — replace with real frames as you tune. */
static const DemoKey attract_path[] = {
  /* intro (incl. post-tunnel outro) */
  {    0, DIR_LEFT },
  {   40, DIR_DOWN },
  {   80, DIR_RIGHT },
  {  120, DIR_DOWN },
  {  160, DIR_RIGHT },
  {  200, DIR_UP },
  {  240, DIR_LEFT },
  {  280, DIR_UP },
  {  320, DIR_RIGHT },
  {  360, DIR_UP },
  {  400, DIR_LEFT },
  {  440, DIR_UP },
  {  480, DIR_LEFT },
  {  520, DIR_DOWN },
  {  560, DIR_LEFT },
  {  600, DIR_UP },
  {  640, DIR_LEFT },
  {  680, DIR_DOWN },
  {  720, DIR_RIGHT },
  {  760, DIR_UP },
  {  800, DIR_LEFT },
  {  840, DIR_DOWN },
  {  880, DIR_LEFT },
  {  920, DIR_UP },
  {  960, DIR_RIGHT },
  { 1000, DIR_DOWN },
  { 1040, DIR_RIGHT },
  { 1080, DIR_DOWN },
  { 1120, DIR_LEFT },
  { 1160, DIR_DOWN },
  { 1200, DIR_LEFT },
  /* after tunnel */
  { 1240, DIR_DOWN },
  { 1280, DIR_RIGHT },
  { 1320, DIR_DOWN },
  /* repeat */
  { 1360, DIR_LEFT },
  { 1400, DIR_UP },
  { 1440, DIR_RIGHT },
  { 1480, DIR_UP },
  { 1520, DIR_LEFT },
  { 1560, DIR_UP },
  { 1600, DIR_RIGHT },
  { 1640, DIR_DOWN },
  { 1680, DIR_RIGHT },
  { 1720, DIR_UP },
  { 1760, DIR_RIGHT },
  { 1800, DIR_DOWN },
  { 1840, DIR_LEFT },
  { 1880, DIR_DOWN },
  { 1920, DIR_RIGHT },
  { 1960, DIR_DOWN },
  { 2000, DEMO_REP },
  { DEMO_END, 0 }
};

static word demo_t;
static byte demo_i;

static void demo_steer(void) {
  for (;;) {
    const DemoKey* n = &attract_path[demo_i + 1];
    if (n->when == DEMO_END)
      break;
    if (demo_t < n->when)
      break;
    if (n->dir == DEMO_REP) {
      demo_i = ATTRACT_PATH_LOOP;
      demo_t = attract_path[demo_i].when;
      break;
    }
    demo_i++;
  }
  pac_want = attract_path[demo_i].dir;
}
#endif /* !ATTRACT_RECORD */

void start_round(byte player_one);
void show_death(void);
void show_game_over(void);

static byte attract_maze_demo(void) {
  attract_demo = 1;
  attract_corridor = 0;
  sound_enable = 0; /* hard mute — no WSG at all */
  lives = 1;
  level = 0;
  score = 0;
  game_over = 0;
  global_dot_mode = 0;
  global_dot_counter = 0;
  clrscr(0);
  start_round(1);
#ifdef ATTRACT_RECORD
  demo_rec_begin(); /* timer = 0, seed LEFT */
#else
  demo_t = 0;
  demo_i = 0;
  pac_want = attract_path[0].dir;
#endif

  while (!(START1 && credits)) {
    wait_vblank();
    watchdog = 0;
    anim_ticks++;
    tick++;
    poll_credit();

    if (freeze_ticks) {
      freeze_ticks--;
      update_ambient();
      draw_hud();
      flash_1up();
      flash_power_pills(1);
      actors_draw();
      continue;
    }

#ifdef ATTRACT_RECORD
    demo_rec_sample();
#else
    demo_steer();
#endif
    pac_update();
    ghosts_update();
    demo_t++;

    if (check_ghost_hits()) {
      show_death();
#ifdef ATTRACT_RECORD
      demo_rec_finish();
      /* Hold so you can dump RAM 4CB0; START (with credit) exits. */
      while (!(START1 && credits)) {
        wait_vblank();
        watchdog = 0;
        poll_credit();
      }
      attract_demo = 0;
      sound_enable = 1;
      sfx_off();
      return 1;
#else
      show_game_over();
      attract_demo = 0;
      sound_enable = 1;
      sfx_off();
      return 0;
#endif
    }

    if (power_ticks) power_ticks--;
    update_ambient();
    draw_hud();
    flash_1up();
    flash_power_pills(1);
    actors_draw();
#ifdef ATTRACT_RECORD
    /* Live key count while training (no / — avoids __divuint) */
    put_u8_dec3(25, 0, demo_rec_n, 0x0F);
#endif
  }

  attract_demo = 0;
  sound_enable = 1;
  sfx_off();
  return 1;
}

#ifndef ATTRACT_MAZE_ONLY
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
  byte chase_done;

restart_attract:
  clrscr(0);
  hide_all_sprites();
  sfx_off();
  attract_demo = 0;
  attract_corridor = 0;
  coin_was_down = COIN1 ? 1 : 0;

  /* Player score shows 00 on attract; keep hiscore. */
  score = 0;
  hud_ready = 0;
  draw_hud();
  put_string(7, 5, "CHARACTER / NICKNAME", 0x0F);
  draw_credits();

  t = 0;
  chase_done = 0;
  /* Intro → chase → maze demo; START with credits leaves. */
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
      if (START1 && credits) break;
      /* Maze demo: death → replay title; START+credit → play. */
      if (!attract_maze_demo())
        goto restart_attract;
      break;
    }
  }

  while (START1) {
    wait_vblank();
    watchdog = 0;
    poll_credit();
  }
  if (credits) credits--;
  attract_demo = 0;
  attract_corridor = 0;
  sfx_off();
}
#endif /* !ATTRACT_MAZE_ONLY */

void show_death(void) {
  byte t, frame;
  byte i;
  word timeout;
  for (i = 1; i < 8; i++) hide_sprite(i);
  if (!attract_demo)
    play_sfx(4);
  for (t = 0; t < 88; t++) {
    frame = (byte)(SP_DEATH0 + (t >> 3));
    if (frame > SP_DEATH_LAST) frame = SP_DEATH_LAST;
    set_sprite_ex(0, frame, PAL_YELLOW,
                  (byte)((pac_tx << 3) + pac_ox - 8),
                  (byte)((pac_ty << 3) + pac_oy - 8), 0);
    wait_vblank();
    watchdog = 0;
    if (!attract_demo && t == 72) CH3_E_NUM = 0x20;
  }
  hide_sprite(0);
  if (attract_demo) return;
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
  if (attract_demo)
    actors_draw(); /* maze attract: no PLAYER/READY — just go */
  else
    show_ready_banner(player_one);
}

#ifndef ATTRACT_MAZE_ONLY
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
#endif /* !ATTRACT_MAZE_ONLY */

void main(void) {
  sound_enable = 1;
  flip_screen = 0;
  watchdog = 0;
  video_framecount = 0;
  rnd = 0xCACE;
  sfx_off(); /* arms pac_vblank_hook + clears WSG */
  pac_irq_enable();

  /* ATTRACT_DEV / ATTRACT_RECORD: maze demo loop. Else: title → play. */
#if defined(ATTRACT_DEV) || defined(ATTRACT_RECORD)
  while (1)
    attract_maze_demo();
#else
  while (1) {
    title_screen();
    game_loop();
    show_game_over();
  }
#endif
}
