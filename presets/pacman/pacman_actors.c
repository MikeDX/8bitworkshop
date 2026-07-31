#pragma opt_code_speed
#include "pacman_actors.h"
#include "pacman_maze.h"
#include "pacman_sfx.h"

/*
 * =============================================================================
 * Pac-Man Dossier rules (Jamey Pittman) — ghosts
 * Tags: [CONFIRM] already matched; [CHANGE] fixed here; [GAP] known approx.
 * Arcade level N == our (level+1).  level==0 is first maze.
 * =============================================================================
 *
 * MODES
 *  [CONFIRM] Three modes: SCATTER, CHASE, FRIGHT (plus house/eyes travel).
 *  [CONFIRM] Reverse next_dir on chase↔scatter and *→fright; NOT on fright→*.
 *  [CONFIRM] round_ticks paused while power_ticks>0 (fright pauses S/C timer).
 *
 * SCATTER / CHASE SCHEDULE (seconds) — Table in dossier §modes
 *  L1:      S7 C20 S7 C20 S5 C20 S5  then chase forever
 *  L2–4:    S7 C20 S7 C20 S5 C1033 S(1/60) then chase forever
 *  L5+:     S5 C20 S5 C20 S5 C1037 S(1/60) then chase forever
 *  [CHANGE] was hard-coded to L1 timings for every level.
 *
 * SCATTER CORNERS (tile targets, unreachable → corner patrol)
 *  [CONFIRM] Blinky(25,0) Pinky(2,0) Inky(27,34) Clyde(0,34)
 *
 * CHASE TARGETS
 *  [CONFIRM] Blinky → Pac tile
 *  [CONFIRM] Pinky  → 4 tiles ahead; UP also −4 X (overflow bug)
 *  [CONFIRM] Inky   → 2 ahead (same UP bug), then 2× vector from Blinky
 *  [CONFIRM] Clyde  → Pac if Euclidean²>64 (8 tiles), else his scatter tile
 *
 * FRIGHTENED
 *  [CHANGE] PRNG picks first try dir, then clockwise until legal
 *  [CHANGE] Red-zones ignored while frightened (may turn UP)
 *  [CHANGE] Full Table A.1 fright seconds (0 on L17/19/20/21+ → reverse only)
 *  [CHANGE] Flash count from A.1; energizer always reverses S/C/F ghosts
 *  [CONFIRM] Per-ghost frightened: set on ALL at pill; clear on power end or respawn
 *
 * HOUSE RELEASE
 *  [CONFIRM] Blinky outside; house: Inky left, Pinky middle, Clyde right
 *  [CONFIRM] Personal limits: Pinky=0 always; L1 Inky30/Clyde60;
 *            L2 Inky0/Clyde50; L3+ all 0
 *  [CONFIRM] Only one personal counter active; preference Pinky→Inky→Clyde
 *  [CONFIRM] After death: global counter 7/17/32; personal kept (not reset)
 *  [CHANGE]  Force-exit timer: 4s L1–4, 3s L5+; only ONE preferred ghost
 *  [CONFIRM] Eating a dot resets the force-exit timer
 *
 * CRUISE ELROY (Blinky)
 *  [CHANGE]  Dot thresholds from Table A.1 (was fixed 20/10)
 *  [CHANGE]  After death Elroy suspended until Clyde leaves house
 *  [CONFIRM] Elroy uses Pac as scatter target; Elroy2 = Pac speed +5%
 *
 * SPEEDS (8.8 fixed-point; bands L1 / L2–4 / L5+)
 *  [CONFIRM] Pac normal 80/90/100%; fright 90/95/100%; L21+ Pac 90%
 *  [CONFIRM] Ghost cruise 75/85/95%; fright 50/55/60%; tunnel 40/45/50%
 *  [CONFIRM] Eyes 150%; house/leave 50%; Elroy1=Pac, Elroy2=Pac+5%
 *
 * RED-ZONE
 *  [CONFIRM] No UP at x=11..16, y=14|26 in scatter/chase (not fright/eyes)
 * =============================================================================
 */

/* Ghost colors / scatter corners (tile coords) — dossier §scatter */
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
  byte i;
  phase_mode = scatter_chase_mode();
  if (!power_ticks) {
    for (i = 0; i < GHOST_N; i++)
      ghosts[i].frightened = 0;
  }
}

#pragma opt_code_size
void set_house_limits(void) {
  /* [CONFIRM] dossier personal dot limits (Pinky always 0) */
  ghosts[0].dot_limit = 0;
  ghosts[1].dot_limit = 0;
#ifdef ENABLE_CURLY
  ghosts[4].dot_limit = 0;
#endif
#ifdef ENABLE_ZOMBIE
  ghosts[5].dot_limit = 0;
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

/* [CHANGE] Table A.1 Elroy1 / Elroy2 dots-remaining (pairs). */
static const byte elroy_dots[] = {
  /* L1 */ 20, 10, /*2*/ 30, 15, /*3-5*/ 40, 20, /*6-8*/ 50, 25,
  /*9-11*/ 60, 30, /*12-14*/ 80, 40, /*15-18*/ 100, 50, /*19+*/ 120, 60
};

void update_elroy(void) {
  byte idx;
  if (elroy_suspended) {
    elroy = 0;
    return;
  }
  if (level == 0) idx = 0;
  else if (level == 1) idx = 2;
  else if (level < 5) idx = 4;
  else if (level < 8) idx = 6;
  else if (level < 11) idx = 8;
  else if (level < 14) idx = 10;
  else if (level < 18) idx = 12;
  else idx = 14;
  if (dots_left <= elroy_dots[idx + 1]) elroy = 2;
  else if (dots_left <= elroy_dots[idx]) elroy = 1;
  else elroy = 0;
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

/* [CONFIRM] dossier speed bands: L1 / L2–4 / L5+ (arcade); L21+ Pac 90% */
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

  if (mode == MODE_HOUSE || mode == MODE_LEAVE) return SP_50; /* [CONFIRM] */
  if (mode == MODE_FRIGHT) return spd_fright_g[b];
  if (mode == MODE_EYES || mode == MODE_ENTER) return SP_150; /* [CONFIRM] */
  ty = (byte)(g->y >> 3);
  tx = (byte)(g->x >> 3);
  /* [CONFIRM] tunnel slowdown on y=17, x<=5 or x>=22 */
  if (ty == 17 && (tx <= 5 || tx >= 22)) return spd_tunnel_g[b];
  if (g == ghosts) {
    /* [CONFIRM] Elroy1 = Pac speed; Elroy2 = Pac + ~5% */
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

#pragma opt_code_size
void move_pos(word* px, word* py, byte dir, byte cornering) {
  sbyte dx, dy;
  if (dir < 1 || dir > 4) return;
  dx = dir_dx[dir];
  dy = dir_dy[dir];
  if (dx) {
    word x = *px;
    if (dx < 0) {
      if (x) x--; else x = 223;
    } else {
      x++;
      if (x >= 224) x = 0;
    }
    *px = x;
  } else {
    *py = (word)(*py + dy);
  }
  if (!cornering) return;
  if (dx) {
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
/* ---- ghost AI (dossier) ---- */

/* tileΔ² for pathfinding / Clyde (0..31). Avoids __mulint. */
static const word tile_sqr[32] = {
  0, 1, 4, 9, 16, 25, 36, 49, 64, 81, 100, 121, 144, 169, 196, 225,
  256, 289, 324, 361, 400, 441, 484, 529, 576, 625, 676, 729, 784, 841, 900, 961
};

static word dist2_tiles(byte ax, byte ay) {
  if (ax > 31) ax = 31;
  if (ay > 31) ay = 31;
  return (word)(tile_sqr[ax] + tile_sqr[ay]);
}

/* Scatter/chase phase ends (frames). L1 / L2–4 / L5+. Index 0..6 = S C S C S C S. */
static const word sc_bounds[3][7] = {
  { 420, 1620, 2040, 3240, 3540, 4740, 5040 },     /* L1: … C20 S5 */
  { 420, 1620, 2040, 3240, 3540, 65520, 65521 },   /* L2–4: C1033 S1f */
  { 300, 1500, 1800, 3000, 3300, 65520, 65521 }    /* L5+: C1037 S1f */
};

/*
 * [CHANGE] Level-dependent scatter/chase (was always L1 schedule).
 * round_ticks paused during power_ticks — [CONFIRM].
 */
static byte scatter_chase_mode(void) {
  word t = round_ticks;
  byte row, i;
  if (level == 0) row = 0;
  else if (level < 4) row = 1;
  else row = 2;
  for (i = 0; i < 7; i++) {
    if (t < sc_bounds[row][i])
      return (i & 1) ? MODE_CHASE : MODE_SCATTER;
  }
  return MODE_CHASE;
}

/* Target tile for current ghost AI decision (avoids pointer out-params). */
static byte ai_tx, ai_ty;

/* Prefer order for chase/scatter; clockwise order for fright. */
static const byte dirs_pref[4] = { DIR_UP, DIR_LEFT, DIR_DOWN, DIR_RIGHT };
static const byte dirs_cw[4] = { DIR_UP, DIR_RIGHT, DIR_DOWN, DIR_LEFT };

static void ghost_target(byte i) {
  Ghost* g = &ghosts[i];
  byte ptx = (byte)(pac_x >> 3);
  byte pty = (byte)(pac_y >> 3);
  byte pd = pac_dir;
  sbyte pdx, pdy;
  byte mode = g->mode;

  if (pd > 4) pd = DIR_RIGHT;
  pdx = dir_dx[pd];
  pdy = dir_dy[pd];

  if (mode == MODE_EYES || mode == MODE_LEAVE || mode == MODE_ENTER ||
      mode == MODE_HOUSE) {
    ai_tx = 13;
    ai_ty = 14;
    return;
  }

  if (mode == MODE_SCATTER) {
    if (i == 0 && elroy) {
      ai_tx = ptx;
      ai_ty = pty;
    } else {
      ai_tx = scat_x[i];
      ai_ty = scat_y[i];
    }
    return;
  }

  /* MODE_CHASE (fright uses PRNG path, not targets) */
  if (i == 0) {
    ai_tx = ptx;
    ai_ty = pty;
  } else if (i == 1) {
    /* Pinky: 4 ahead; UP also −4 X */
    ai_tx = (byte)((sbyte)ptx + (sbyte)(pdx << 2));
    ai_ty = (byte)((sbyte)pty + (sbyte)(pdy << 2));
    if (pd == DIR_UP)
      ai_tx = (byte)((sbyte)ai_tx - 4);
  } else if (i == 2) {
    /* Inky: 2 ahead (UP bug), then 2×(that − Blinky) */
    byte bx = (byte)(ghosts[0].x >> 3);
    byte by = (byte)(ghosts[0].y >> 3);
    sbyte px2 = (sbyte)(ptx + (sbyte)(pdx << 1));
    sbyte py2 = (sbyte)(pty + (sbyte)(pdy << 1));
    if (pd == DIR_UP) px2 = (sbyte)(px2 - 2);
    ai_tx = (byte)(bx + (sbyte)((px2 - (sbyte)bx) << 1));
    ai_ty = (byte)(by + (sbyte)((py2 - (sbyte)by) << 1));
  } else {
    /* Clyde */
    if (dist2_tiles(abs_diff((byte)(g->x >> 3), ptx),
                    abs_diff((byte)(g->y >> 3), pty)) > 64) {
      ai_tx = ptx;
      ai_ty = pty;
    } else {
      ai_tx = scat_x[i];
      ai_ty = scat_y[i];
    }
  }
}

/* 1 if dir is open from lookahead tile (not reverse, not blocked). */
static byte dir_open(byte lx, byte ly, byte d, byte gdir) {
  byte nx, ny;
  if (opp_dir[d] == gdir) return 0;
  nx = (byte)((sbyte)lx + dir_dx[d]);
  ny = (byte)((sbyte)ly + dir_dy[d]);
  if (nx >= 28 || ny >= 36) return 0;
  return !tile_blocked(nx, ny);
}

byte update_ghost_dir(byte i) {
  Ghost* g = &ghosts[i];
  byte di, d, best, gdir, lx, ly, mode;

  mode = g->mode;

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
    if ((byte)(g->x >> 3) == 14 || g->x == ANTE_X) {
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
  if (gdir > 4) gdir = DIR_RIGHT;
  g->dir = gdir;
  lx = (byte)((sbyte)(g->x >> 3) + dir_dx[gdir]);
  ly = (byte)((sbyte)(g->y >> 3) + dir_dy[gdir]);

  if (mode == MODE_FRIGHT) {
    byte start = (byte)(rand8() & 3);
    best = gdir;
    for (di = 0; di < 4; di++) {
      d = dirs_cw[(byte)((start + di) & 3)];
      if (dir_open(lx, ly, d, gdir)) {
        best = d;
        break;
      }
    }
    g->next_dir = best;
    return 0;
  }

  ghost_target(i);
  best = gdir;
  {
    word best_dist = 0xffff;
    for (di = 0; di < 4; di++) {
      word dist;
      d = dirs_pref[di];
      if (d == DIR_UP && mode != MODE_EYES &&
          lx >= 11 && lx <= 16 && (ly == 14 || ly == 26))
        continue;
      if (!dir_open(lx, ly, d, gdir)) continue;
      dist = dist2_tiles(
        abs_diff((byte)((sbyte)lx + dir_dx[d]), ai_tx),
        abs_diff((byte)((sbyte)ly + dir_dy[d]), ai_ty));
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
    /* Force-exit handled in ghosts_update (one preferred ghost only). */
    if (global_dot_mode) {
      /* [CONFIRM] after death: Pinky@7 Inky@17 Clyde@32 → deactivate global */
      if (i == 1 && global_dot_counter == 7) new_mode = MODE_LEAVE;
      if (i == 2 && global_dot_counter == 17) new_mode = MODE_LEAVE;
      if (i == 3 && global_dot_counter == 32) {
        new_mode = MODE_LEAVE;
        global_dot_mode = 0;
      }
    } else if (i >= 1 && i <= 3 && g->dot_counter >= g->dot_limit) {
      /* [CONFIRM] personal counter; Pinky limit 0 → leave immediately */
      new_mode = MODE_LEAVE;
    }
    break;
  case MODE_LEAVE:
    if (g->y == ANTE_Y)
      new_mode = g->frightened ? MODE_FRIGHT : phase_mode;
    break;
  default:
    new_mode = g->frightened ? MODE_FRIGHT : phase_mode;
    break;
  }

  if (new_mode != mode) {
    if (mode == MODE_LEAVE) {
      g->dir = g->next_dir = DIR_LEFT;
    } else if (mode == MODE_SCATTER || mode == MODE_CHASE) {
      /* [CONFIRM] reverse on chase↔scatter; fright reverse is in try_eat_tile */
      if (new_mode == MODE_SCATTER || new_mode == MODE_CHASE)
        g->next_dir = opp_dir[g->dir];
    }
    g->mode = new_mode;
    /* [CONFIRM] respawn in house clears fright for this ghost only */
    if (new_mode == MODE_ENTER)
      g->frightened = 0;
    if (new_mode == MODE_LEAVE) {
      /* [CHANGE] Elroy resumes when Clyde leaves after a death */
      if (i == 3 && elroy_suspended) {
        elroy_suspended = 0;
        update_elroy();
      }
    }
  }
}
