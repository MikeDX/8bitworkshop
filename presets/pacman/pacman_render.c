#pragma opt_code_speed
#include "pacman_actors.h"
#include "pacman_maze.h"
#include "pacman_sfx.h"
#include "pacman_assets.h"

extern const byte ghost_pal[GHOST_N];
extern const byte scat_x[GHOST_N];
extern const byte scat_y[GHOST_N];

/* Last tile where we already tried to eat — peek only on tile entry. */
static byte pac_eat_tx;
static byte pac_eat_ty;
static byte fruit_shown;
/* Mouth phase — ALWAYS 0..3 (closed, partial, wide, partial). Tick only if moved. */
static byte pac_anim;

byte abs_diff(byte a, byte b) {
  return (a > b) ? (byte)(a - b) : (byte)(b - a);
}


byte rand8(void) {
  rnd = rnd * 17 + 53;
  return (byte)(rnd >> 8);
}


#pragma opt_code_size
void actors_reset_level(void) {
  byte i;

  pac_x = 14 * 8;
  pac_y = 26 * 8 + 4;
  pac_frac = 0;
  pac_dir = DIR_LEFT;
  pac_want = DIR_LEFT;
  pac_dead = 0;
  pac_eat_tx = 0xff;
  pac_eat_ty = 0xff;
  pac_anim = 0;
  power_ticks = 0;
  eat_combo = 0;
  round_ticks = 0;
  elroy = 0;
  fruit_ticks = 0;
  fruit_visible = 0;
  fruit_shown = 0;
  pac_stop = 0;
  force_house = 0;
  freeze_ticks = 0;
  eyes_present = 0;

  for (i = 0; i < GHOST_N; i++) {
    Ghost* g = &ghosts[i];
    g->color = ghost_pal[i];
    g->scat_x = scat_x[i];
    g->scat_y = scat_y[i];
    g->dot_counter = 0;
    g->frac = 0;
    g->speed_sig = 0xff;
    g->x = 14 * 8;
    g->y = 17 * 8 + 4;
    g->dir = DIR_UP;
    g->next_dir = DIR_UP;
    g->mode = MODE_HOUSE;
    g->in_house = 1;
  }
  set_house_limits();

  /* Blinky outside (extras may shift him left on the ante row). */
  ghosts[0].y = 14 * 8 + 4;
  ghosts[0].dir = DIR_LEFT;
  ghosts[0].next_dir = DIR_LEFT;
  ghosts[0].mode = MODE_SCATTER;
  ghosts[0].in_house = 0;

#if defined(ENABLE_ZOMBIE)
  /* 3 above house over Inky/Pinky/Clyde columns. */
  ghosts[0].x = 12 * 8;
  ghosts[4].x = 14 * 8;
  ghosts[4].y = 14 * 8 + 4;
  ghosts[4].dir = DIR_LEFT;
  ghosts[4].next_dir = DIR_LEFT;
  ghosts[4].mode = MODE_SCATTER;
  ghosts[4].in_house = 0;
  ghosts[5].x = 16 * 8;
  ghosts[5].y = 14 * 8 + 4;
  ghosts[5].dir = DIR_LEFT;
  ghosts[5].next_dir = DIR_LEFT;
  ghosts[5].mode = MODE_SCATTER;
  ghosts[5].in_house = 0;
#elif defined(ENABLE_CURLY)
  /* Curly alone: Blinky left, Curly in classic Blinky slot. */
  ghosts[0].x = 12 * 8;
  ghosts[4].x = 14 * 8;
  ghosts[4].y = 14 * 8 + 4;
  ghosts[4].dir = DIR_LEFT;
  ghosts[4].next_dir = DIR_LEFT;
  ghosts[4].mode = MODE_SCATTER;
  ghosts[4].in_house = 0;
#endif

  /* Pinky middle */
  ghosts[1].dir = DIR_DOWN;
  ghosts[1].next_dir = DIR_DOWN;

  /* Inky left / Clyde right */
  ghosts[2].x = 12 * 8;
  ghosts[3].x = 16 * 8;
}

#pragma opt_code_speed
byte check_ghost_hits(void) {
  byte i;
  byte ptx = (byte)(pac_x >> 3);
  byte pty = (byte)(pac_y >> 3);
  Ghost* g = ghosts;

  for (i = 0; i < GHOST_N; i++, g++) {
    byte mode = g->mode;
    if (mode >= MODE_EYES)
      continue;
    if ((byte)(g->x >> 3) != ptx)
      continue;
    if ((byte)(g->y >> 3) != pty)
      continue;
    if (mode == MODE_FRIGHT) {
      byte combo = eat_combo;
      if (combo > 3) combo = 3;
      play_sfx(3);
      if (combo == 0) score += 20;
      else if (combo == 1) score += 40;
      else if (combo == 2) score += 80;
      else score += 160;
      freeze_score = combo;
      freeze_ghost = i;
      freeze_ticks = EAT_FREEZE_TICKS;
      eat_combo = (byte)(combo + 1);
      g->mode = MODE_EYES;
      eyes_present = 1;
      return 0;
    }
    return 1;
  }

  if (fruit_visible &&
      (byte)((pac_x + 4) >> 3) == FRUIT_TX &&
      (byte)(pac_y >> 3) == FRUIT_TY) {
    score += 10;
    fruit_visible = 0;
    fruit_ticks = 90;
  }
  return 0;
}

/* ---- sprite frame tables (dir: 1=R 2=D 3=L 4=U) ---- */
static const byte pac_flags_tbl[5] = { 0, 0, 0, FLIP_X, FLIP_Y };
/* phase 0..3: closed, partial, wide, partial */
static const byte pac_shape_h[4] = { SP_CLOSED, SP_PAC_R, SP_PAC_R_WIDE, SP_PAC_R };
static const byte pac_shape_v[4] = { SP_CLOSED, SP_PAC_D, SP_PAC_D_WIDE, SP_PAC_D };

/* Ghost body base tile per dir; +anim (0/1) selects frame */
static const byte ghost_dir_base[5] = {
  SP_GHOST_R0, SP_GHOST_R0, SP_GHOST_D0, SP_GHOST_L0, SP_GHOST_U0
};
static const byte score_spr[4] = {
  SP_SCORE200, SP_SCORE400, SP_SCORE800, SP_SCORE1600
};

/* Sprite top-left from center: sx=cx-8 → reg 239-sx = 247-cx.
 * Macro (not a call) — draw is ~20% of useful work. */
#define PAC_DRAW(i, shape, pal, cx, cy, fl) do { \
  byte* _a = (byte*)(0x4ff0 + ((byte)(i) << 1)); \
  byte* _p = (byte*)(0x5060 + ((byte)(i) << 1)); \
  _a[0] = (byte)(((byte)(shape) << 2) | ((byte)(fl) & 3)); \
  _a[1] = (byte)(pal); \
  _p[0] = (byte)(247 - (byte)(cx)); \
  _p[1] = (byte)(280 - (byte)(cy)); \
} while (0)

void actors_draw_anim(byte animate) {
  byte i, sh, pal, anim, dir;
  byte eyes_pal;
  Ghost* g;

  anim = animate ? (byte)((anim_ticks >> 2) & 1) : 0;
  eyes_pal = attract_demo ? 0 : PAL_EYES;

  if (freeze_ticks) {
    hide_sprite(0);
  } else {
    dir = pac_dir;
    if (dir < 1 || dir > 4) dir = DIR_RIGHT;
    {
      byte phase = animate ? (byte)(pac_anim & 3) : 0;
      sh = (dir == DIR_UP || dir == DIR_DOWN)
           ? pac_shape_v[phase] : pac_shape_h[phase];
    }
    PAC_DRAW(0, sh, PAL_YELLOW, pac_x, pac_y, pac_flags_tbl[dir]);
  }

  for (i = 0; i < GHOST_N; i++) {
    g = &ghosts[i];
    dir = g->dir;
    if (dir > 4) dir = DIR_RIGHT;

    if (freeze_ticks && i == freeze_ghost &&
        (g->mode == MODE_EYES || g->mode == MODE_ENTER)) {
      sh = score_spr[freeze_score < 4 ? freeze_score : 3];
      pal = PAL_GHOST_SCORE;
    } else if (g->mode == MODE_EYES || g->mode == MODE_ENTER) {
      sh = (byte)(ghost_dir_base[dir] + anim);
      pal = eyes_pal;
    } else if (g->mode == MODE_FRIGHT) {
      sh = (byte)(SP_SCARED0 + anim);
      pal = (power_ticks < 60 && (anim_ticks & 0x10))
            ? PAL_SCARED_BLINK : PAL_SCARED;
    } else {
      sh = (byte)(ghost_dir_base[dir] + anim);
      pal = g->color;
    }
    PAC_DRAW((byte)(i + 1), sh, pal, g->x, g->y, 0);
  }

  if (fruit_visible) {
    byte fruit = level;
    if (fruit > 7) fruit = 7;
    PAC_DRAW(SPR_FRUIT, (byte)(SP_FRUIT0 + fruit), PAL_FRUIT,
            (word)(FRUIT_TX * 8 + 4), (word)(FRUIT_TY * 8 + 4), 0);
    fruit_visible--;
    fruit_shown = 1;
  } else if (fruit_shown) {
    hide_sprite(SPR_FRUIT);
    fruit_shown = 0;
  }
}

void actors_draw(void) {
  actors_draw_anim(1);
}

/* Advance 8.8 accumulator; return whole pixels to step this frame. */
byte take_steps(word* frac, word speed) {
  word sum = (word)(*frac + speed);
  *frac = (word)(sum & 0xff);
  return (byte)(sum >> 8);
}

void pac_update(void) {
  byte tx, ty, steps, s, moved;

  if (!attract_demo) {
    if (LEFT1) pac_want = DIR_LEFT;
    if (RIGHT1) pac_want = DIR_RIGHT;
    if (UP1) pac_want = DIR_UP;
    if (DOWN1) pac_want = DIR_DOWN;
  }

  /* Dot/energizer pause: skip movement only this/these frames, then
   * resume full pac_speed_fp() — ghosts keep moving meanwhile. */
  if (pac_stop) {
    pac_stop--;
    return;
  }

  /* Attract corridor: X-only, no tunnel wrap (spawn/enter from off-screen). */
  if (attract_demo) {
    if (pac_want != pac_dir)
      pac_dir = pac_want;
    moved = 0;
    steps = take_steps(&pac_frac, pac_speed_fp());
    for (s = 0; s < steps; s++) {
      if (pac_dir == DIR_LEFT) {
        if (pac_x > 0) pac_x--;
        else break;
      } else if (pac_dir == DIR_RIGHT) {
        if (pac_x < 255) pac_x++;
        else break;
      } else
        break;
      moved = 1;
    }
    pac_y = (word)(20 * 8 + 4);
    if (moved)
      pac_anim = (byte)((pac_anim + 1) & 3);
    if (pac_x >= 8 && pac_x < 224) {
      tx = tile_x(pac_x);
      ty = tile_y(pac_y);
      if (tx != pac_eat_tx || ty != pac_eat_ty) {
        pac_eat_tx = tx;
        pac_eat_ty = ty;
        try_eat_tile(tx, ty);
      }
    }
    return;
  }

  moved = 0;
  steps = take_steps(&pac_frac, pac_speed_fp());
  for (s = 0; s < steps; s++) {
    /* Cornering / reverse: only probe when player asks for a new dir.
     * can_move itself skips VRAM peeks until leave-tile mid. */
    if (pac_want != pac_dir && can_move(pac_x, pac_y, pac_want, 1))
      pac_dir = pac_want;
    if (!can_move(pac_x, pac_y, pac_dir, 1))
      break;
    move_pos(&pac_x, &pac_y, pac_dir, 1);
    moved = 1;
  }
  /* Simple rule: moved → tick (clamped 0..3). stopped → freeze. */
  if (moved)
    pac_anim = (byte)((pac_anim + 1) & 3);

  tx = tile_x(pac_x);
  ty = tile_y(pac_y);
  if (tx != pac_eat_tx || ty != pac_eat_ty) {
    pac_eat_tx = tx;
    pac_eat_ty = ty;
    try_eat_tile(tx, ty);
  }
}

void ghosts_update(void) {
  byte i, steps, s;
  Ghost* g;

  if (!power_ticks)
    round_ticks++;
  force_house++;

  ghost_frame_begin(); /* phase_mode once for all ghosts */
  eyes_present = 0;

  for (i = 0; i < GHOST_N; i++) {
    update_ghost_state(i);
    g = &ghosts[i];
    if (g->mode == MODE_EYES || g->mode == MODE_ENTER)
      eyes_present = 1;
    steps = take_steps(&g->frac, ghost_speed_cached(g));
    for (s = 0; s < steps; s++) {
      byte mode = g->mode;
      if (mode == MODE_HOUSE || mode == MODE_LEAVE || mode == MODE_ENTER) {
        update_ghost_dir(i);
        move_pos(&g->x, &g->y, g->dir, 0);
      } else if (AT_TILE_MID(g->x, g->y)) {
        update_ghost_dir(i);
        if (!can_move(g->x, g->y, g->dir, 0))
          break;
        move_pos(&g->x, &g->y, g->dir, 0);
      } else {
        /* Corridor between decision points — path already chosen. */
        move_pos(&g->x, &g->y, g->dir, 0);
      }
    }
  }
}
