#pragma opt_code_speed
#include "pacman_actors.h"
#include "pacman_maze.h"
#include "pacman_sfx.h"

/* Ghost colors / scatter corners (tile coords) — floooh */
const byte ghost_pal[GHOST_N] = {
  PAL_BLINKY, PAL_PINKY, PAL_INKY, PAL_CLYDE
#ifdef ENABLE_CURLY
  , PAL_CURLY
#endif
#ifdef ENABLE_ZOMBIE
  , PAL_FRED
#endif
};
const byte scat_x[GHOST_N] = {
  25, 2, 27, 0
#ifdef ENABLE_CURLY
  , 14
#endif
#ifdef ENABLE_ZOMBIE
  , 0
#endif
};
const byte scat_y[GHOST_N] = {
  0, 0, 34, 34
#ifdef ENABLE_CURLY
  , 17
#endif
#ifdef ENABLE_ZOMBIE
  , 17
#endif
};

/* Hot-path dir lookups */
const sbyte dir_dx[5] = { 0, 1, 0, -1, 0 };
const sbyte dir_dy[5] = { 0, 0, 1, 0, -1 };
const byte opp_dir[5] = { 0, DIR_LEFT, DIR_UP, DIR_RIGHT, DIR_DOWN };

/* Scatter/chase phase — computed once per ghosts_update */
static byte phase_mode;
static byte scatter_chase_mode(void);

void ghost_frame_begin(void) {
  phase_mode = scatter_chase_mode();
}

#pragma opt_code_size
void set_house_limits(void) {
  ghosts[0].dot_limit = 0;
  ghosts[1].dot_limit = 0; /* Pinky always immediate */
#ifdef ENABLE_CURLY
  ghosts[4].dot_limit = 0; /* Curly starts outside */
#endif
#ifdef ENABLE_ZOMBIE
  ghosts[5].dot_limit = 0; /* Fred starts outside */
#endif
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
  if (d > 4) return DIR_NONE;
  return opp_dir[d];
}

byte tile_x(word px) { return (byte)(px >> 3); }
byte tile_y(word py) { return (byte)(py >> 3); }

/*
 * Dossier speeds as 8.8 fixed-point (100% = 0x140 ≈ 1.25 px/frame).
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

/* File-scope const — stays in CODE (not INITIALIZED). */
static const word spd_fright_pac[3] = { SP_90, SP_95, SP_100 };
static const word spd_normal_pac[3] = { SP_80, SP_90, SP_100 };
static const word spd_fright_g[3] = { SP_50, SP_55, SP_60 };
static const word spd_tunnel_g[3] = { SP_40, SP_45, SP_50 };
static const word spd_cruise_g[3] = { SP_75, SP_85, SP_95 };

static byte level_band(void) {
  if (level == 0) return 0;
  if (level < 4) return 1;
  return 2;
}

word pac_speed_fp(void) {
  byte b = level_band();
  if (power_ticks) return spd_fright_pac[b];
  if (level >= 20) return SP_90;
  return spd_normal_pac[b];
}

word ghost_speed_fp(Ghost* g) {
  byte mode = g->mode;
  byte b = level_band();
  byte tx, ty;

  if (mode == MODE_HOUSE || mode == MODE_LEAVE) return SP_50;
  if (mode == MODE_FRIGHT) return spd_fright_g[b];
  if (mode == MODE_EYES || mode == MODE_ENTER) return SP_150;
  ty = (byte)(g->y >> 3);
  tx = (byte)(g->x >> 3);
  if (ty == 17 && (tx <= 5 || tx >= 22)) return spd_tunnel_g[b];
  if (g == ghosts) {
    if (elroy == 2) return (word)(pac_speed_fp() + 0x0010);
    if (elroy == 1) return pac_speed_fp();
  }
  return spd_cruise_g[b];
}

word ghost_speed_cached(Ghost* g) {
  byte sig = g->mode;
  byte ty = (byte)(g->y >> 3);
  if (ty == 17) {
    byte tx = (byte)(g->x >> 3);
    if (tx <= 5 || tx >= 22) sig |= 0x80;
  }
  if (g == ghosts) sig |= (byte)(elroy << 4);
  if (sig != g->speed_sig) {
    g->speed_sig = sig;
    g->speed_fp = ghost_speed_fp(g);
  }
  return g->speed_fp;
}

#pragma opt_code_speed
/* floooh can_move — Pac allows cornering. */
byte can_move(word px, word py, byte dir, byte cornering) {
  sbyte dx, dy;
  sbyte move_mid, perp_mid;
  byte tx, ty, nx, ny;

  if (dir == DIR_NONE || dir > 4) return 0;
  dx = dir_dx[dir];
  dy = dir_dy[dir];

  if (dy != 0) {
    move_mid = (sbyte)(4 - (byte)(py & 7));
    perp_mid = (sbyte)(4 - (byte)(px & 7));
  } else {
    move_mid = (sbyte)(4 - (byte)(px & 7));
    perp_mid = (sbyte)(4 - (byte)(py & 7));
  }

  if (!cornering && perp_mid != 0) return 0;
  if (move_mid != 0) return 1;

  tx = (byte)(px >> 3);
  ty = (byte)(py >> 3);
  nx = (byte)((sbyte)tx + dx);
  ny = (byte)((sbyte)ty + dy);

  if (nx >= 28) nx = 0;
  if ((sbyte)((sbyte)tx + dx) < 0) nx = 27;

  return !tile_blocked(nx, ny);
}

#pragma opt_code_speed
void move_pos(word* px, word* py, byte dir, byte cornering) {
  switch (dir) {
  case DIR_LEFT:
    if (*px) (*px)--; else *px = 223;
    break;
  case DIR_RIGHT:
    (*px)++;
    if (*px >= 224) *px = 0;
    break;
  case DIR_UP:
    (*py)--;
    break;
  case DIR_DOWN:
    (*py)++;
    break;
  default:
    return;
  }

  if (!cornering) return;

  if (dir == DIR_LEFT || dir == DIR_RIGHT) {
    byte m = (byte)(*py) & 7;
    if (m < 4) (*py)++;
    else if (m > 4) (*py)--;
  } else {
    byte m = (byte)(*px) & 7;
    if (m < 4) (*px)++;
    else if (m > 4) (*px)--;
  }
}

#pragma opt_code_size
/* ---- ghost AI (floooh) ---- */

#pragma opt_code_size
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
  byte ptx = (byte)(pac_x >> 3);
  byte pty = (byte)(pac_y >> 3);
  byte pd = pac_dir;
  sbyte pdx, pdy;

  if (pd > 4) pd = DIR_RIGHT;
  pdx = dir_dx[pd];
  pdy = dir_dy[pd];

  if (g->mode == MODE_SCATTER) {
    if (i == 0 && elroy) {
      *tx = ptx;
      *ty = pty;
    } else {
      *tx = g->scat_x;
      *ty = g->scat_y;
    }
  } else if (g->mode == MODE_FRIGHT) {
    /* No % — avoids linking ~1KB div lib. */
    *tx = (byte)(((word)rand8() * 28) >> 8);
    *ty = (byte)(((word)rand8() * 36) >> 8);
  } else if (g->mode == MODE_EYES) {
    *tx = 13;
    *ty = 14;
  } else if (g->mode == MODE_CHASE) {
    if (i == 0) {
      *tx = ptx;
      *ty = pty;
    } else if (i == 1) {
      *tx = (byte)((sbyte)ptx + pdx * 4);
      *ty = (byte)((sbyte)pty + pdy * 4);
      if (pac_dir == DIR_UP)
        *tx = (byte)((sbyte)(*tx) - 4);
    } else if (i == 2) {
      byte bx = (byte)(ghosts[0].x >> 3);
      byte by = (byte)(ghosts[0].y >> 3);
      sbyte px2 = (sbyte)(ptx + pdx * 2);
      sbyte py2 = (sbyte)(pty + pdy * 2);
      if (pac_dir == DIR_UP) px2 = (sbyte)(px2 - 2);
      *tx = (byte)(bx + (px2 - (sbyte)bx) * 2);
      *ty = (byte)(by + (py2 - (sbyte)by) * 2);
    } else if (i == 3) { /* Clyde */
      word ddx = abs_diff((byte)(g->x >> 3), ptx);
      word ddy = abs_diff((byte)(g->y >> 3), pty);
      if (ddx * ddx + ddy * ddy > 64) {
        *tx = ptx;
        *ty = pty;
      } else {
        *tx = g->scat_x;
        *ty = g->scat_y;
      }
#ifdef ENABLE_CURLY
    } else if (i == 4) { /* Curly the Idiot — wanders at random */
      *tx = (byte)(((word)rand8() * 28) >> 8);
      *ty = (byte)(((word)rand8() * 36) >> 8);
#endif
#ifdef ENABLE_ZOMBIE
    } else if (i == 5) { /* Fred the Zombie — always hunts Pac */
      *tx = ptx;
      *ty = pty;
#endif
    }
  } else {
    *tx = 13;
    *ty = 14;
  }
}

byte update_ghost_dir(byte i) {
  Ghost* g = &ghosts[i];
  byte tgt_x, tgt_y;
  byte di, d, best;
  word best_dist, dist;
  sbyte dx, dy;
  byte lx, ly, nx, ny;
  byte gdir;
  byte mode = g->mode;

  if (mode == MODE_HOUSE) {
    if (g->y <= (word)(17 * 8)) g->next_dir = DIR_DOWN;
    else if (g->y >= (word)(18 * 8)) g->next_dir = DIR_UP;
    g->dir = g->next_dir;
    return 1;
  }

  if (mode == MODE_LEAVE) {
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

  if (mode == MODE_ENTER) {
    byte tx = (byte)(g->x >> 3);
    if (tx == 14 || g->x == ANTE_X) {
      if (g->x != ANTE_X)
        g->next_dir = (g->x < ANTE_X) ? DIR_RIGHT : DIR_LEFT;
      else
        g->next_dir = DIR_DOWN;
    } else if ((byte)(g->y >> 3) == 14) {
      g->next_dir = (g->x < ANTE_X) ? DIR_RIGHT : DIR_LEFT;
    } else {
      g->next_dir = DIR_DOWN;
    }
    g->dir = g->next_dir;
    return 1;
  }

  if (!AT_TILE_MID(g->x, g->y)) return 0;

  gdir = g->next_dir;
  g->dir = gdir;
  ghost_target(i, &tgt_x, &tgt_y);

  if (gdir > 4) gdir = DIR_RIGHT;
  dx = dir_dx[gdir];
  dy = dir_dy[gdir];
  lx = (byte)((sbyte)(g->x >> 3) + dx);
  ly = (byte)((sbyte)(g->y >> 3) + dy);

  best = gdir;
  best_dist = 0xffff;

  /* Prefer U,L,D,R (arcade tie-break). Stack fill — no INITIALIZED. */
  {
    byte dirs[4];
    dirs[0] = DIR_UP;
    dirs[1] = DIR_LEFT;
    dirs[2] = DIR_DOWN;
    dirs[3] = DIR_RIGHT;
    for (di = 0; di < 4; di++) {
      d = dirs[di];
      /* redzone: no UP except eyes */
      if (d == DIR_UP && mode != MODE_EYES &&
          lx >= 11 && lx <= 16 && (ly == 14 || ly == 26))
        continue;
      if (opp_dir[d] == gdir) continue;
      dx = dir_dx[d];
      dy = dir_dy[d];
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
  }
  g->next_dir = best;
  return 0;
}

void update_ghost_state(byte i) {
  Ghost* g = &ghosts[i];
  byte new_mode = g->mode;
  byte mode = g->mode;

  switch (mode) {
  case MODE_EYES:
    if (abs_diff((byte)g->x, ANTE_X) <= 1 &&
        abs_diff((byte)g->y, ANTE_Y) <= 1)
      new_mode = MODE_ENTER;
    break;
  case MODE_ENTER:
    if ((byte)(g->y >> 3) >= 17)
      new_mode = MODE_LEAVE;
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
    if (g->y == ANTE_Y)
      new_mode = phase_mode;
    break;
  default:
    new_mode = power_ticks ? MODE_FRIGHT : phase_mode;
    break;
  }

  if (new_mode != mode) {
    if (mode == MODE_LEAVE) {
      g->dir = g->next_dir = DIR_LEFT;
    } else if (mode == MODE_SCATTER || mode == MODE_CHASE) {
      if (new_mode == MODE_SCATTER || new_mode == MODE_CHASE ||
          new_mode == MODE_FRIGHT)
        g->next_dir = opp_dir[g->dir];
    }
    g->mode = new_mode;
    if (new_mode == MODE_LEAVE) g->in_house = 0;
  }
}
