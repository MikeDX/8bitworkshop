#pragma opt_code_speed
#include "pacman_actors.h"
#include "pacman_maze.h"
#include "pacman_sfx.h"

typedef signed int sword;



/* Ghost colors / scatter corners (tile coords) — floooh */
const byte ghost_pal[GHOST_N] = {
  PAL_BLINKY, PAL_PINKY, PAL_INKY, PAL_CLYDE
};
const byte scat_x[GHOST_N] = { 25, 2, 27, 0 };
const byte scat_y[GHOST_N] = { 0, 0, 34, 34 };

void set_house_limits(void) {
  ghosts[0].dot_limit = 0;
  ghosts[1].dot_limit = 0; /* Pinky always immediate */
  if (level == 0) {
    ghosts[2].dot_limit = 30;
    ghosts[3].dot_limit = 60;
  } else if (level == 1) {
    ghosts[2].dot_limit = 0;
    ghosts[3].dot_limit = 50;
  } else {
    ghosts[2].dot_limit = 0;
    ghosts[3].dot_limit = 0;
  }
}

byte opposite_dir(byte d) {
  if (d == DIR_LEFT) return DIR_RIGHT;
  if (d == DIR_RIGHT) return DIR_LEFT;
  if (d == DIR_UP) return DIR_DOWN;
  if (d == DIR_DOWN) return DIR_UP;
  return DIR_NONE;
}

void dir_vec(byte d, sbyte* dx, sbyte* dy) {
  *dx = 0;
  *dy = 0;
  if (d == DIR_LEFT) *dx = -1;
  else if (d == DIR_RIGHT) *dx = 1;
  else if (d == DIR_UP) *dy = -1;
  else if (d == DIR_DOWN) *dy = 1;
}

/* Pixel center → tile (floooh pixel_to_tile_pos) */
byte tile_x(word px) { return (byte)(px >> 3); }
byte tile_y(word py) { return (byte)(py >> 3); }

/* Distance to tile midpoint: mid when (pos % 8) == 4 */
static sbyte dist_mid_x(word px) { return (sbyte)(4 - (byte)(px & 7)); }
static sbyte dist_mid_y(word py) { return (sbyte)(4 - (byte)(py & 7)); }

static byte is_tunnel(byte tx, byte ty) {
  return (ty == 17) && (tx <= 5 || tx >= 22);
}

static byte is_redzone(byte tx, byte ty) {
  return (tx >= 11 && tx <= 16) && (ty == 14 || ty == 26);
}

/*
 * Dossier speeds as 8.8 fixed-point (100% = 0x140 ≈ 1.25 px/frame).
 * Precomputed — never divide by 100 in the hot path.
 *
 * Pac: normal / fright by level. Eating dots adds a 1-frame move pause
 * (3 for energizer) so effective speed drops to ~71% while munching, then
 * returns to normal on empty path. Pac is NOT slowed in tunnels.
 *
 * Ghosts: tunnel ~half; fright slower; eyes ~150%; Elroy matches/beats Pac.
 */
#define SP_40   0x0080
#define SP_45   0x0090
#define SP_50   0x00A0
#define SP_55   0x00B0
#define SP_60   0x00C0
#define SP_75   0x00F0
#define SP_80   0x0100
#define SP_85   0x0110
#define SP_90   0x0120
#define SP_95   0x0130
#define SP_100  0x0140
#define SP_150  0x01E0

word pac_speed_fp(void) {
  if (power_ticks) {
    if (level == 0) return SP_90;
    if (level < 4) return SP_95;
    return SP_100;
  }
  if (level == 0) return SP_80;
  if (level < 4) return SP_90;
  if (level < 20) return SP_100;
  return SP_90;
}

word ghost_speed_fp(Ghost* g) {
  byte tx = tile_x(g->x);
  byte ty = tile_y(g->y);
  if (g->mode == MODE_HOUSE || g->mode == MODE_LEAVE)
    return SP_50;
  if (g->mode == MODE_FRIGHT) {
    if (level == 0) return SP_50;
    if (level < 4) return SP_55;
    return SP_60;
  }
  if (g->mode == MODE_EYES || g->mode == MODE_ENTER)
    return SP_150;
  if (is_tunnel(tx, ty)) {
    if (level == 0) return SP_40;
    if (level < 4) return SP_45;
    return SP_50;
  }
  if (g == &ghosts[0] && elroy == 2)
    return (word)(pac_speed_fp() + 0x0010); /* Pac + 5% */
  if (g == &ghosts[0] && elroy == 1)
    return pac_speed_fp();
  if (level == 0) return SP_75;
  if (level < 4) return SP_85;
  return SP_95;
}

/* Recompute only when mode / tunnel / Elroy changes. */
word ghost_speed_cached(Ghost* g) {
  byte sig = g->mode;
  if (is_tunnel(tile_x(g->x), tile_y(g->y))) sig |= 0x80;
  if (g == &ghosts[0]) sig |= (byte)(elroy << 4);
  if (sig != g->speed_sig) {
    g->speed_sig = sig;
    g->speed_fp = ghost_speed_fp(g);
  }
  return g->speed_fp;
}

/* floooh can_move — Pac allows cornering.
 * Wall peeks only matter when leave-tile decision is due (move_mid == 0). */
byte can_move(word px, word py, byte dir, byte cornering) {
  sbyte dx, dy;
  sbyte move_mid, perp_mid;
  byte tx, ty, nx, ny;

  if (dir == DIR_NONE) return 0;
  dir_vec(dir, &dx, &dy);

  if (dy != 0) {
    move_mid = dist_mid_y(py);
    perp_mid = dist_mid_x(px);
  } else {
    move_mid = dist_mid_x(px);
    perp_mid = dist_mid_y(py);
  }

  if (!cornering && perp_mid != 0) return 0;
  if (move_mid != 0) return 1; /* still in tile — no wall peek yet */

  tx = tile_x(px);
  ty = tile_y(py);
  nx = (byte)((sbyte)tx + dx);
  ny = (byte)((sbyte)ty + dy);

  if (nx >= 28) nx = 0;
  if ((sbyte)((sbyte)tx + dx) < 0) nx = 27;

  return !tile_blocked(nx, ny);
}

void move_pos(word* px, word* py, byte dir, byte cornering) {
  sbyte dx, dy;
  sbyte dmx, dmy;
  dir_vec(dir, &dx, &dy);
  *px = (word)((sword)(*px) + dx);
  *py = (word)((sword)(*py) + dy);

  if (cornering) {
    dmx = dist_mid_x(*px);
    dmy = dist_mid_y(*py);
    if (dx != 0) {
      if (dmy < 0) (*py)--;
      else if (dmy > 0) (*py)++;
    } else if (dy != 0) {
      if (dmx < 0) (*px)--;
      else if (dmx > 0) (*px)++;
    }
  }

  /* tunnel wrap — display is 224 px wide */
  if ((sword)(*px) < 0) *px = 223;
  else if (*px >= 224) *px = 0;
}

/* ---- ghost AI (floooh) ---- */

static byte scatter_chase_mode(void) {
  word t = round_ticks;
  if (t < 7 * 60) return MODE_SCATTER;
  if (t < 27 * 60) return MODE_CHASE;
  if (t < 34 * 60) return MODE_SCATTER;
  if (t < 54 * 60) return MODE_CHASE;
  if (t < 59 * 60) return MODE_SCATTER;
  if (t < 79 * 60) return MODE_CHASE;
  if (t < 84 * 60) return MODE_SCATTER;
  return MODE_CHASE;
}

static void ghost_target(byte i, byte* tx, byte* ty) {
  Ghost* g = &ghosts[i];
  byte ptx = tile_x(pac_x);
  byte pty = tile_y(pac_y);
  sbyte pdx, pdy;
  dir_vec(pac_dir, &pdx, &pdy);

  if (g->mode == MODE_SCATTER) {
    /* Cruise Elroy keeps chasing during scatter */
    if (i == 0 && elroy) {
      *tx = ptx;
      *ty = pty;
    } else {
      *tx = g->scat_x;
      *ty = g->scat_y;
    }
  } else if (g->mode == MODE_FRIGHT) {
    *tx = rand8() % 28;
    *ty = rand8() % 36;
  } else if (g->mode == MODE_EYES) {
    *tx = 13;
    *ty = 14;
  } else if (g->mode == MODE_CHASE) {
    if (i == 0) { /* Blinky */
      *tx = ptx;
      *ty = pty;
    } else if (i == 1) { /* Pinky: 4 ahead (up overflow bug: also 4 left) */
      *tx = (byte)((sbyte)ptx + pdx * 4);
      *ty = (byte)((sbyte)pty + pdy * 4);
      if (pac_dir == DIR_UP)
        *tx = (byte)((sbyte)(*tx) - 4);
    } else if (i == 2) { /* Inky */
      {
        byte bx = tile_x(ghosts[0].x);
        byte by = tile_y(ghosts[0].y);
        sbyte px2 = (sbyte)(ptx + pdx * 2);
        sbyte py2 = (sbyte)(pty + pdy * 2);
        if (pac_dir == DIR_UP) px2 = (sbyte)(px2 - 2); /* same overflow bug as Pinky */
        *tx = (byte)(bx + (px2 - (sbyte)bx) * 2);
        *ty = (byte)(by + (py2 - (sbyte)by) * 2);
      }
    } else { /* Clyde */
      {
        word ddx = abs_diff(tile_x(g->x), ptx);
        word ddy = abs_diff(tile_y(g->y), pty);
        if (ddx * ddx + ddy * ddy > 64) {
          *tx = ptx;
          *ty = pty;
        } else {
          *tx = g->scat_x;
          *ty = g->scat_y;
        }
      }
    }
  } else {
    *tx = 13;
    *ty = 14;
  }
}

byte update_ghost_dir(byte i) {
  Ghost* g = &ghosts[i];
  byte tx, tgt_x, tgt_y;
  byte dirs[4];
  byte di, d, rev, best;
  word best_dist, dist;
  sbyte dx, dy;
  byte lx, ly, nx, ny;

  /* bob inside house */
  if (g->mode == MODE_HOUSE) {
    if (g->y <= (word)(17 * 8)) g->next_dir = DIR_DOWN;
    else if (g->y >= (word)(18 * 8)) g->next_dir = DIR_UP;
    g->dir = g->next_dir;
    return 1;
  }

  /* leave house toward anteportas */
  if (g->mode == MODE_LEAVE) {
    if (g->x == ANTE_X) {
      if (g->y > ANTE_Y) g->next_dir = DIR_UP;
    } else {
      word mid_y = (word)(17 * 8 + 4);
      if (g->y > mid_y) g->next_dir = DIR_UP;
      else if (g->y < mid_y) g->next_dir = DIR_DOWN;
      else g->next_dir = (g->x > ANTE_X) ? DIR_LEFT : DIR_RIGHT;
    }
    g->dir = g->next_dir;
    return 1;
  }

  /* enter house */
  if (g->mode == MODE_ENTER) {
    tx = tile_x(g->x);
    if (tx == 14 || g->x == ANTE_X) {
      if (g->x != ANTE_X)
        g->next_dir = (g->x < ANTE_X) ? DIR_RIGHT : DIR_LEFT;
      else
        g->next_dir = DIR_DOWN;
    } else if (tile_y(g->y) == 14) {
      g->next_dir = (g->x < ANTE_X) ? DIR_RIGHT : DIR_LEFT;
    } else {
      g->next_dir = DIR_DOWN;
    }
    g->dir = g->next_dir;
    return 1;
  }

  /* scatter/chase/fright/eyes: decide at tile mid */
  if (!AT_TILE_MID(g->x, g->y)) return 0;

  g->dir = g->next_dir;
  ghost_target(i, &tgt_x, &tgt_y);

  dir_vec(g->dir, &dx, &dy);
  lx = (byte)((sbyte)tile_x(g->x) + dx);
  ly = (byte)((sbyte)tile_y(g->y) + dy);

  dirs[0] = DIR_UP;
  dirs[1] = DIR_LEFT;
  dirs[2] = DIR_DOWN;
  dirs[3] = DIR_RIGHT;
  best = g->dir;
  best_dist = 0xffff;

  for (di = 0; di < 4; di++) {
    d = dirs[di];
    if (is_redzone(lx, ly) && d == DIR_UP && g->mode != MODE_EYES)
      continue;
    rev = opposite_dir(d);
    if (rev == g->dir) continue;
    dir_vec(d, &dx, &dy);
    nx = (byte)((sbyte)lx + dx);
    ny = (byte)((sbyte)ly + dy);
    if (nx >= 28 || ny >= 36) continue;
    if (tile_blocked(nx, ny)) continue;
    {
      word ax = abs_diff(nx, tgt_x);
      word ay = abs_diff(ny, tgt_y);
      dist = ax * ax + ay * ay;
    }
    if (dist < best_dist) {
      best_dist = dist;
      best = d;
    }
  }
  g->next_dir = best;
  return 0;
}

void update_ghost_state(byte i) {
  Ghost* g = &ghosts[i];
  byte new_mode = g->mode;
  byte phase;

  switch (g->mode) {
  case MODE_EYES:
    if (abs_diff((byte)g->x, ANTE_X) <= 1 &&
        abs_diff((byte)g->y, ANTE_Y) <= 1)
      new_mode = MODE_ENTER;
    break;
  case MODE_ENTER:
    if (tile_y(g->y) >= 17) {
      new_mode = MODE_LEAVE;
    }
    break;
  case MODE_HOUSE:
    if (force_house >= 4 * 60) {
      new_mode = MODE_LEAVE;
      force_house = 0;
    } else if (global_dot_mode) {
      if (i == 1 && global_dot_counter == 7) new_mode = MODE_LEAVE;
      if (i == 2 && global_dot_counter == 17) new_mode = MODE_LEAVE;
      if (i == 3 && global_dot_counter == 32) {
        new_mode = MODE_LEAVE;
        global_dot_mode = 0;
      }
    } else if (i > 0 && g->dot_counter >= g->dot_limit) {
      new_mode = MODE_LEAVE;
    }
    break;
  case MODE_LEAVE:
    if (g->y == ANTE_Y) {
      new_mode = scatter_chase_mode();
    }
    break;
  default:
    if (power_ticks)
      new_mode = MODE_FRIGHT;
    else {
      phase = scatter_chase_mode();
      new_mode = phase;
    }
    break;
  }

  if (new_mode != g->mode) {
    if (g->mode == MODE_LEAVE) {
      g->dir = g->next_dir = DIR_LEFT;
    } else if (g->mode == MODE_SCATTER || g->mode == MODE_CHASE) {
      /* Reverse on scatter↔chase and when entering fright */
      if (new_mode == MODE_SCATTER || new_mode == MODE_CHASE || new_mode == MODE_FRIGHT)
        g->next_dir = opposite_dir(g->dir);
    }
    /* Leaving fright does NOT reverse (dossier) */
    g->mode = new_mode;
    if (new_mode == MODE_LEAVE) g->in_house = 0;
  }
}

