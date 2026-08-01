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
 *  [CHANGE] PRNG: first try dir, then clockwise (same algorithm as arcade).
 *           Entropy is our LCG / ROM image — not Midway’s bytes — so fright
 *           routes will not match stock arcade 1:1. Scatter/chase can.
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
 *  [CONFIRM] Eating a regular dot resets the force-exit timer (not energizers)
 *  [CONFIRM] House/global counters increment on regular dots only
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
 *  [CONFIRM] Arcade (#1c0b): at mid, color of *current* tile RAM (4d0a) ==
 *           0x1A → skip AI. 4d0a equals occupancy at mid (advance is after AI),
 *           so check g->tx/ty — NOT lookahead. Lookahead skip kept DOWN into
 *           the house door and stuck ghosts.
 *  [CONFIRM] While pathfinding (approaching), forbid UP if lookahead in zone.
 *  [CONFIRM] Fright skips the check (UP into those tunnels allowed).
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
static byte phase_prev; /* 0xff = unset */
static byte scatter_chase_mode(void);

/* Toggled on S↔C while HOUSE/LEAVE/ENTER; consumed on door exit. */
static byte ghost_exit_flip[GHOST_N];

void ghost_frame_begin(void) {
  byte i;
  phase_mode = scatter_chase_mode();
  if (phase_prev != 0xff && phase_prev != phase_mode &&
      (phase_prev == MODE_SCATTER || phase_prev == MODE_CHASE) &&
      (phase_mode == MODE_SCATTER || phase_mode == MODE_CHASE)) {
    /* Housed ghosts don't change mode, but exit facing reverses. */
    for (i = 0; i < GHOST_N; i++) {
      byte m = ghosts[i].mode;
      if (m == MODE_HOUSE || m == MODE_LEAVE || m == MODE_ENTER)
        ghost_exit_flip[i] ^= 1;
    }
  }
  phase_prev = phase_mode;
  if (!power_ticks) {
    for (i = 0; i < GHOST_N; i++)
      ghosts[i].frightened = 0;
  }
}

#pragma opt_code_size
void set_house_limits(void) {
  byte i;
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
  for (i = 0; i < GHOST_N; i++)
    ghost_exit_flip[i] = 0;
  phase_prev = 0xff;
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

/*
 * Dossier Table A.1: 100% = 75.75757625 px/s @ 60Hz → 1.262626… px/frame.
 * 8.8 fixed-point: round(pct/100 * 75.75757625/60 * 256).
 */
#define SP_40   0x0081  /* 40% */
#define SP_45   0x0091
#define SP_50   0x00A2
#define SP_55   0x00B2
#define SP_60   0x00C2
#define SP_75   0x00F2  /* ghost cruise L1 */
#define SP_80   0x0103  /* Pac L1 */
#define SP_85   0x0113
#define SP_90   0x0123
#define SP_95   0x0133
#define SP_100  0x0143
#define SP_150  0x01E5  /* eyes */
#define SP_ELROY2_BONUS 0x0010  /* +5% of max ≈ Elroy2 vs Elroy1 */

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

  if (mode == MODE_HOUSE || mode == MODE_LEAVE) return SP_50; /* [CONFIRM] */
  if (mode == MODE_FRIGHT) return spd_fright_g[b];
  if (mode == MODE_EYES || mode == MODE_ENTER) return SP_150; /* [CONFIRM] */
  /* [CONFIRM] tunnel slowdown on y=17, x<=5 or x>=22 */
  if (g->ty == 17 && (g->tx <= 5 || g->tx >= 22)) return spd_tunnel_g[b];
  if (g == ghosts) {
    /* [CONFIRM] Elroy1 = Pac speed; Elroy2 = Pac + ~5% */
    if (elroy == 2) return (word)(pac_speed_fp() + SP_ELROY2_BONUS);
    if (elroy == 1) return pac_speed_fp();
  }
  return spd_cruise_g[b];
}

word ghost_speed_cached(Ghost* g) {
  byte sig = g->mode;
  if (g->ty == 17) {
    if (g->tx <= 5 || g->tx >= 22) sig |= 0x80;
  }
  if (g == ghosts) sig |= (byte)(elroy << 4);
  if (sig != g->speed_sig) {
    g->speed_sig = sig;
    g->speed_fp = ghost_speed_fp(g);
  }
  return g->speed_fp;
}

#pragma opt_code_speed
/* floooh can_move — Pac allows cornering. pos = {tx,ty,ox,oy}. */
byte can_move(byte* pos, byte dir, byte cornering) {
  sbyte dx, dy;
  sbyte move_mid, perp_mid;
  byte nx, ny;
  byte tx = pos[0], ty = pos[1], ox = pos[2], oy = pos[3];

  if (dir == DIR_NONE || dir > 4) return 0;
  dx = dir_dx[dir];
  dy = dir_dy[dir];

  if (dy != 0) {
    move_mid = (sbyte)(4 - oy);
    perp_mid = (sbyte)(4 - ox);
  } else {
    move_mid = (sbyte)(4 - ox);
    perp_mid = (sbyte)(4 - oy);
  }

  if (!cornering && perp_mid != 0) return 0;
  if (move_mid != 0) return 1;

  nx = (byte)((sbyte)tx + dx);
  ny = (byte)((sbyte)ty + dy);

  if (nx >= 28) nx = 0;
  if ((sbyte)((sbyte)tx + dx) < 0) nx = 27;

  return !tile_blocked(nx, ny);
}

#pragma opt_code_size
/* One-pixel step. pos = {tx,ty,ox,oy} (Ghost / pac_*). */
void move_pos(byte* pos, byte dir) {
  byte* t;
  byte* o;
  byte horiz;

  if (dir < DIR_RIGHT || dir > DIR_UP) return;
  horiz = (byte)(dir == DIR_LEFT || dir == DIR_RIGHT);
  if (horiz) { t = pos; o = pos + 2; }
  else { t = pos + 1; o = pos + 3; }

  if (dir == DIR_LEFT || dir == DIR_UP) {
    if (*o) (*o)--;
    else if (*t) { (*t)--; *o = 7; }
    else if (horiz) { *t = 27; *o = 7; } /* tunnel wrap L only */
  } else {
    (*o)++;
    if (*o >= 8) {
      *o = 0;
      (*t)++;
      if (horiz && *t >= 28) *t = 0; /* tunnel wrap R */
    }
  }
}

/* Pac: step then ease perpendicular offset toward 4. */
void move_pos_pac(byte* pos, byte dir) {
  byte* o;
  move_pos(pos, dir);
  if (dir == DIR_LEFT || dir == DIR_RIGHT) o = pos + 3;
  else if (dir == DIR_UP || dir == DIR_DOWN) o = pos + 2;
  else return;
  if (*o < 4) (*o)++;
  else if (*o > 4) (*o)--;
}

#pragma opt_code_size
/* ---- ghost AI (dossier + Midway ASM pacman.asm.txt) ----
 *
 * Decision timing (#1bf7–#1c36): at tile mid, dir←next_dir, tile RAM is
 * advanced by next_dir *before* consumers (Inky reads Blinky @ #27cb from
 * that advanced tile). Prefer order via #2966 / #32ff → UP wins ties.
 * Red zone (#1c0b–#1c14): color 0x1A on *current* tile at mid → skip AI.
 * Fright (#1bfe): no red-zone check — UP allowed.
 * House exit: normally LEFT; reverse while housed → RIGHT (#dossier).
 */

/* Squared Euclidean without __mulint/__div* (keep _CODE under tile_rom). */
static const word tile_sqr[64] = {
  0,1,4,9,16,25,36,49,64,81,100,121,144,169,196,225,
  256,289,324,361,400,441,484,529,576,625,676,729,784,841,900,961,
  1024,1089,1156,1225,1296,1369,1444,1521,1600,1681,1764,1849,1936,2025,2116,2209,
  2304,2401,2500,2601,2704,2809,2916,3025,3136,3249,3364,3481,3600,3721,3844,3969
};

static word dist2_to(sbyte x, sbyte y, sbyte tx, sbyte ty) {
  byte dx, dy;
  {
    int d = (int)x - (int)tx;
    if (d < 0) d = -d;
    dx = (d > 63) ? 63 : (byte)d;
  }
  {
    int d = (int)y - (int)ty;
    if (d < 0) d = -d;
    dy = (d > 63) ? 63 : (byte)d;
  }
  return (word)(tile_sqr[dx] + tile_sqr[dy]);
}

/* #1c1c: after mid, ghost tile RAM = occupancy + dir until pixel wraps into
 * that tile. Match that for Inky←Blinky and Clyde radius. */
static void ghost_tile_ai(Ghost* g, byte* otx, byte* oty) {
  byte d = g->dir;
  byte ahead = 0;
  if (d < 1 || d > 4) d = DIR_LEFT;
  if (d == DIR_RIGHT) ahead = (g->ox >= 4);
  else if (d == DIR_LEFT) ahead = (g->ox <= 4);
  else if (d == DIR_DOWN) ahead = (g->oy >= 4);
  else ahead = (g->oy <= 4); /* UP */
  if (ahead) {
    sbyte nx = (sbyte)((sbyte)g->tx + dir_dx[d]);
    sbyte ny = (sbyte)((sbyte)g->ty + dir_dy[d]);
    if (nx < 0) nx = 27;
    else if (nx >= 28) nx = 0;
    *otx = (byte)nx;
    *oty = (byte)ny;
  } else {
    *otx = g->tx;
    *oty = g->ty;
  }
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
static sbyte ai_tx, ai_ty;

/* Last computed targets — for GHOST_AI_DEBUG overlay. */
sbyte ghost_ai_tx[GHOST_N];
sbyte ghost_ai_ty[GHOST_N];

/* Prefer order for chase/scatter; clockwise order for fright. */
static const byte dirs_pref[4] = { DIR_UP, DIR_LEFT, DIR_DOWN, DIR_RIGHT };
static const byte dirs_cw[4] = { DIR_UP, DIR_RIGHT, DIR_DOWN, DIR_LEFT };

static void ghost_target(byte i) {
  Ghost* g = &ghosts[i];
  sbyte ptx = (sbyte)pac_tx;
  sbyte pty = (sbyte)pac_ty;
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
  } else if (mode == MODE_SCATTER) {
    if (i == 0 && elroy) {
      ai_tx = ptx;
      ai_ty = pty;
    } else {
      ai_tx = (sbyte)scat_x[i];
      ai_ty = (sbyte)scat_y[i];
    }
  } else if (mode == MODE_FRIGHT) {
    /* Fright has no tile target — park marker on ghost. */
    ai_tx = (sbyte)g->tx;
    ai_ty = (sbyte)g->ty;
  } else if (i == 0) {
    /* MODE_CHASE */
    ai_tx = ptx;
    ai_ty = pty;
  } else if (i == 1) {
    /* Pinky: 4 ahead; UP also −4 X */
    ai_tx = (sbyte)(ptx + (sbyte)(pdx << 2));
    ai_ty = (sbyte)(pty + (sbyte)(pdy << 2));
    if (pd == DIR_UP)
      ai_tx = (sbyte)(ai_tx - 4);
  } else if (i == 2) {
    /* Inky: pivot 2 ahead of Pac (UP also −2 X), target = 2*pivot − Blinky.
     * Blinky tile from arcade-advanced 4D0A (#27cb), not occupancy. */
    sbyte pivx, pivy;
    byte bx, by;
    ghost_tile_ai(&ghosts[0], &bx, &by);
    pivx = (sbyte)(ptx + (sbyte)(pdx << 1));
    pivy = (sbyte)(pty + (sbyte)(pdy << 1));
    if (pd == DIR_UP)
      pivx = (sbyte)(pivx - 2);
    ai_tx = (sbyte)(pivx + (pivx - (sbyte)bx));
    ai_ty = (sbyte)(pivy + (pivy - (sbyte)by));
  } else {
    /* Clyde: chase Pac only if farther than 8 tiles (dist² > 64).
     * Dossier / flooh use >; arcade jp-c is strict <64 for retreat —
     * at exactly 8 tiles, > keeps him retreating (safer, classic feel).
     * Occupancy tile (not AI-ahead) for the radius test. */
    if (dist2_to((sbyte)g->tx, (sbyte)g->ty, ptx, pty) > 64) {
      ai_tx = ptx;
      ai_ty = pty;
    } else {
      ai_tx = (sbyte)scat_x[i];
      ai_ty = (sbyte)scat_y[i];
    }
  }

  ghost_ai_tx[i] = ai_tx;
  ghost_ai_ty[i] = ai_ty;
}

/* 1 if dir is open from lookahead tile (not reverse, not blocked). */
static byte dir_open(byte lx, byte ly, byte d, byte gdir) {
  byte nx, ny;
  if (opp_dir[d] == gdir) return 0;
  nx = (byte)((sbyte)lx + dir_dx[d]);
  ny = (byte)((sbyte)ly + dir_dy[d]);
  /* Tunnel wrap (same as can_move / move_pos) — else AI rejects exits. */
  if ((sbyte)((sbyte)lx + dir_dx[d]) < 0) nx = 27;
  else if (nx >= 28) nx = 0;
  if (ny >= 36) return 0;
  return !tile_blocked(nx, ny);
}

byte update_ghost_dir(byte i) {
  Ghost* g = &ghosts[i];
  byte di, d, best, gdir, lx, ly, mode;

  mode = g->mode;
  ghost_target(i); /* always refresh ghost_ai_tx/ty (debug overlay) */

  if (mode == MODE_HOUSE) {
    /* bounce between y=17*8 and 18*8 */
    if (g->ty < 17 || (g->ty == 17 && g->oy == 0))
      g->next_dir = DIR_DOWN;
    else if (g->ty >= 18)
      g->next_dir = DIR_UP;
    g->dir = g->next_dir;
    return 1;
  }

  if (mode == MODE_LEAVE) {
    if (g->tx == ANTE_TX && g->ox == ANTE_OX) {
      if (g->ty > ANTE_TY || (g->ty == ANTE_TY && g->oy > ANTE_OY))
        g->next_dir = DIR_UP;
    } else {
      /* mid_y = 17*8+4 → ty=17, oy=4 */
      if (g->ty > 17 || (g->ty == 17 && g->oy > 4))
        g->next_dir = DIR_UP;
      else if (g->ty < 17 || (g->ty == 17 && g->oy < 4))
        g->next_dir = DIR_DOWN;
      else
        g->next_dir = (g->tx > ANTE_TX ||
                       (g->tx == ANTE_TX && g->ox > ANTE_OX))
                        ? DIR_LEFT : DIR_RIGHT;
    }
    g->dir = g->next_dir;
    return 1;
  }

  if (mode == MODE_ENTER) {
    if (g->tx == ANTE_TX) {
      if (g->ox != ANTE_OX)
        g->next_dir = (g->ox < ANTE_OX) ? DIR_RIGHT : DIR_LEFT;
      else
        g->next_dir = DIR_DOWN;
    } else if (g->ty == 14) {
      g->next_dir = (g->tx < ANTE_TX) ? DIR_RIGHT : DIR_LEFT;
    } else {
      g->next_dir = DIR_DOWN;
    }
    g->dir = g->next_dir;
    return 1;
  }

  if (!AT_TILE_MID(g->ox, g->oy)) return 0;

  gdir = g->next_dir;
  if (gdir > 4) gdir = DIR_RIGHT;
  g->dir = gdir;
  /* Advanced tile for pathfinding origin — tunnel-wrap like #2000. */
  {
    sbyte slx = (sbyte)((sbyte)g->tx + dir_dx[gdir]);
    sbyte sly = (sbyte)((sbyte)g->ty + dir_dy[gdir]);
    if (slx < 0) slx = 27;
    else if (slx >= 28) slx = 0;
    lx = (byte)slx;
    ly = (byte)sly;
  }

  /* Red zone (#1c0b): CURRENT tile at mid (arcade 4d0a before advance). */
  if ((mode == MODE_SCATTER || mode == MODE_CHASE) &&
      g->tx >= 11 && g->tx <= 16 && (g->ty == 14 || g->ty == 26))
    return 0;

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

  best = gdir;
  {
    word best_dist = 0xffff;
    for (di = 0; di < 4; di++) {
      word dist;
      sbyte nx, ny;
      d = dirs_pref[di];
      /* No UP into red-zone tiles (approaching); eyes exempt; fright above. */
      if (d == DIR_UP && mode != MODE_EYES &&
          lx >= 11 && lx <= 16 && (ly == 14 || ly == 26))
        continue;
      if (!dir_open(lx, ly, d, gdir)) continue;
      nx = (sbyte)((sbyte)lx + dir_dx[d]);
      ny = (sbyte)((sbyte)ly + dir_dy[d]);
      if (nx < 0) nx = 27;
      else if (nx >= 28) nx = 0;
      dist = dist2_to(nx, ny, ai_tx, ai_ty);
      /* Arcade #29b7: update when new <= best → later (UP) wins ties.
       * dirs_pref puts UP first with strict < — same UP-wins result. */
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
    /* within 1px of ante (112,116) */
    if (g->tx == ANTE_TX && g->ox <= 1 &&
        g->ty == ANTE_TY && abs_diff(g->oy, ANTE_OY) <= 1)
      new_mode = MODE_ENTER;
    break;
  case MODE_ENTER:
    if (g->ty >= 17)
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
    /* Must reach door pixel (14,14)+ox0/oy4 — not just ante row. */
    if (g->tx == ANTE_TX && g->ox == ANTE_OX &&
        g->ty == ANTE_TY && g->oy == ANTE_OY)
      new_mode = g->frightened ? MODE_FRIGHT : phase_mode;
    break;
  default:
    new_mode = g->frightened ? MODE_FRIGHT : phase_mode;
    break;
  }

  if (new_mode != mode) {
    if (mode == MODE_LEAVE) {
      /* Door exit facing: LEFT default; RIGHT if S↔C reversed while housed. */
      g->dir = g->next_dir =
        ghost_exit_flip[i] ? DIR_RIGHT : DIR_LEFT;
      ghost_exit_flip[i] = 0;
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

#if GHOST_AI_DEBUG
/* Color-only overlays are invisible on blank path tiles — stamp glyphs. */
static byte dbg_tx[GHOST_N + 1], dbg_ty[GHOST_N + 1];
static byte dbg_tile[GHOST_N + 1], dbg_pal[GHOST_N + 1], dbg_on[GHOST_N + 1];
static const char dbg_mark[GHOST_N + 1] = { 'B', 'P', 'I', 'C', 'Y' }; /* +Pac */

void ghost_ai_capture_targets(void) {
  byte i;
  for (i = 0; i < GHOST_N; i++)
    ghost_target(i);
}

static byte dbg_clamp_s(sbyte v, byte lim) {
  if (v < 0) return 0;
  if ((byte)v >= lim) return (byte)(lim - 1);
  return (byte)v;
}

static void dbg_restore(byte i) {
  if (!dbg_on[i]) return;
  poke_tile(dbg_tx[i], dbg_ty[i], dbg_tile[i], dbg_pal[i]);
  dbg_on[i] = 0;
}

static void dbg_stamp(byte i, sbyte stx, sbyte sty, byte pal) {
  word a;
  byte tx = dbg_clamp_s(stx, 28);
  byte ty = dbg_clamp_s(sty, 36);
  a = vram_addr(tx, ty);
  dbg_tx[i] = tx;
  dbg_ty[i] = ty;
  dbg_tile[i] = *((byte*)(0x4000 + a));
  dbg_pal[i] = *((byte*)(0x4400 + a));
  dbg_on[i] = 1;
  poke_tile(tx, ty, (byte)dbg_mark[i], pal);
}

void draw_ghost_ai_debug(void) {
  byte i;

  ghost_ai_capture_targets();

  for (i = 0; i <= GHOST_N; i++)
    dbg_restore(i);

  /* Pac tile we think he's on — yellow Y */
  dbg_stamp(GHOST_N, (sbyte)pac_tx, (sbyte)pac_ty, PAL_YELLOW);

  /* Ghost AI targets — B/P/I/C in body colors */
  for (i = 0; i < GHOST_N; i++)
    dbg_stamp(i, ghost_ai_tx[i], ghost_ai_ty[i], ghost_pal[i]);
}
#endif
