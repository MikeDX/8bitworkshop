#pragma opt_code_speed
#include "pacman_maze.h"
#include "pacman_assets.h"
#include "pacman_sfx.h"

/*
 * Playfield tile map is pre-baked by scripts/gen_pacman_assets.py (maze_tiles).
 *
 * Pac-Man middle VRAM (screen y=2..33) is stored as 28 vertical strips of 32
 * bytes at 0x4040 / 0x4440: strip s holds screen x = 27-s, with byte
 * (y-2) along the strip. Maze rows 0..30 sit at screen y=3..33 → strip
 * offsets 1..31. Drawing walks those addresses forward instead of poke_tile.
 */
#define MAZE_Y0    3
#define MAZE_ROWS  31
#define VRAM_MID   ((byte*)0x4040)
#define CRAM_MID   ((byte*)0x4440)

static byte maze_src(byte row, byte col) {
  return maze_tiles[(word)row * 28 + col];
}

void draw_maze(void) {
  byte strip; /* 0 = screen x=27 … 27 = screen x=0 */
  for (strip = 0; strip < 28; strip++) {
    byte x = (byte)(27 - strip);
    byte row;
    /* Skip strip offset 0 (screen y=2, above maze); write offsets 1..31. */
    byte* v = VRAM_MID + (word)strip * 32 + 1;
    byte* c = CRAM_MID + (word)strip * 32 + 1;
    watchdog = 0;
    for (row = 0; row < MAZE_ROWS; row++) {
      byte tile = maze_src(row, x);
      byte pal = PAL_DOT;
      if (tile == T_DOOR) pal = PAL_DOOR;
      else if (tile >= 0xC0) pal = PAL_MAZE;
      *v++ = tile;
      *c++ = pal;
    }
  }
}

void count_dots(void) {
  byte row, col, t;
  dots_left = 0;
  for (row = 0; row < MAZE_ROWS; row++) {
    for (col = 0; col < 28; col++) {
      t = maze_src(row, col);
      if (t == T_DOT_A || t == T_POWER_A) dots_left++;
    }
  }
  dots_eaten = 0;
  elroy = 0;
}

byte maze_tile(byte tx, byte ty) {
  return peek_tile(tx, ty);
}

byte tile_blocked(byte tx, byte ty) {
  if (tx >= 28 || ty >= 36) return 1;
  return peek_tile(tx, ty) >= 0xC0;
}

void try_eat_tile(byte tx, byte ty) {
  byte t;
  if (tx >= 28 || ty >= 36) return;
  t = peek_tile(tx, ty);
  if (t != T_DOT_A && t != T_POWER_A) return;

  poke_tile(tx, ty, T_BLANK_A, 0);
  if (dots_left) dots_left--;
  dots_eaten++;
  force_house = 0;

  if (t == T_POWER_A) {
    play_sfx(6);
    power_ticks = fright_duration();
    eat_combo = 0;
    score += 5;
    pac_stop = 3; /* dossier: 3-frame move pause, then full speed again */
  } else {
    waka ^= 1;
    play_sfx(waka ? 1 : 2);
    score += 1;
    pac_stop = 1; /* dossier: 1-frame move pause while eating */
  }

  if (dots_eaten == 70 || dots_eaten == 170)
    fruit_visible = 600;

  if (dots_left < 20) elroy = 1;
  if (dots_left < 10) elroy = 2;

  if (global_dot_mode) {
    global_dot_counter++;
  } else {
    byte i;
    for (i = 0; i < GHOST_N; i++) {
      if (ghosts[i].dot_counter < ghosts[i].dot_limit) {
        ghosts[i].dot_counter++;
        break;
      }
    }
  }
}

word fright_duration(void) {
  /* Must be word — 6*60=360 does not fit in a byte (was wrapping to 104). */
  if (level == 0) return 6 * 60;
  if (level == 1) return 5 * 60;
  if (level == 2) return 4 * 60;
  if (level < 5) return 3 * 60;
  if (level < 9) return 2 * 60;
  return 1 * 60;
}
