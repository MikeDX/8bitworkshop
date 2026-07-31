#pragma opt_code_speed
#include "pacman_actors.h"
#include "pacman_maze.h"
#include "pacman_sfx.h"
#include "pacman_assets.h"

extern const byte ghost_pal[GHOST_N];
extern const byte scat_x[GHOST_N];
extern const byte scat_y[GHOST_N];

/* Last tile where we already tried to eat — peek only on tile entry. */
static byte pac_eat_tx = 0xff;
static byte pac_eat_ty = 0xff;
static byte fruit_shown;
static byte pac_anim; /* mouth phase; advances only while moving */

byte abs_diff(byte a, byte b) {
  return (a > b) ? (byte)(a - b) : (byte)(b - a);
}


byte rand8(void) {
  rnd = rnd * 17 + 53;
  return (byte)(rnd >> 8);
}


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

  for (i = 0; i < GHOST_N; i++) {
    ghosts[i].color = ghost_pal[i];
    ghosts[i].scat_x = scat_x[i];
    ghosts[i].scat_y = scat_y[i];
    ghosts[i].dot_counter = 0;
    ghosts[i].frac = 0;
    ghosts[i].speed_sig = 0xff; /* force speed recompute */
  }
  set_house_limits();

  /* Blinky outside */
  ghosts[0].x = 14 * 8;
  ghosts[0].y = 14 * 8 + 4;
  ghosts[0].dir = DIR_LEFT;
  ghosts[0].next_dir = DIR_LEFT;
  ghosts[0].mode = MODE_SCATTER;
  ghosts[0].in_house = 0;

  /* Pinky middle house */
  ghosts[1].x = 14 * 8;
  ghosts[1].y = 17 * 8 + 4;
  ghosts[1].dir = DIR_DOWN;
  ghosts[1].next_dir = DIR_DOWN;
  ghosts[1].mode = MODE_HOUSE;
  ghosts[1].in_house = 1;

  /* Inky left */
  ghosts[2].x = 12 * 8;
  ghosts[2].y = 17 * 8 + 4;
  ghosts[2].dir = DIR_UP;
  ghosts[2].next_dir = DIR_UP;
  ghosts[2].mode = MODE_HOUSE;
  ghosts[2].in_house = 1;

  /* Clyde right */
  ghosts[3].x = 16 * 8;
  ghosts[3].y = 17 * 8 + 4;
  ghosts[3].dir = DIR_UP;
  ghosts[3].next_dir = DIR_UP;
  ghosts[3].mode = MODE_HOUSE;
  ghosts[3].in_house = 1;
}

/* ---- sprite frame tables (dir: 1=R 2=D 3=L 4=U) ---- */
/* Pac mouth cycle: closed → partial → wide → partial */
static const byte pac_shape_tbl[4][4] = {
  /* R */ { SP_CLOSED, SP_PAC_R, SP_PAC_R_WIDE, SP_PAC_R },
  /* D */ { SP_CLOSED, SP_PAC_D, SP_PAC_D_WIDE, SP_PAC_D },
  /* L */ { SP_CLOSED, SP_PAC_R, SP_PAC_R_WIDE, SP_PAC_R },
  /* U */ { SP_CLOSED, SP_PAC_D, SP_PAC_D_WIDE, SP_PAC_D },
};
static const byte pac_flags_tbl[5] = { 0, 0, 0, FLIP_X, FLIP_Y };

/* Ghost body base tile per dir; +anim (0/1) selects frame */
static const byte ghost_dir_base[5] = {
  SP_GHOST_R0, SP_GHOST_R0, SP_GHOST_D0, SP_GHOST_L0, SP_GHOST_U0
};
static const byte score_spr[4] = {
  SP_SCORE200, SP_SCORE400, SP_SCORE800, SP_SCORE1600
};

/* center → sprite top-left (floooh actor_to_sprite_pos) */
static void draw_at(byte i, byte shape, byte pal, word cx, word cy, byte fl) {
  set_sprite_ex(i, shape, pal, (byte)(cx - 8), (byte)(cy - 8), fl);
}

void actors_draw(void) {
  byte i, sh, pal, fl, anim, dir;
  Ghost* g;

  anim = (byte)((anim_ticks >> 2) & 1); /* half previous flip rate */

  if (freeze_ticks) {
    hide_sprite(0);
  } else {
    dir = pac_dir;
    if (dir < 1 || dir > 4) dir = DIR_RIGHT;
    draw_at(0, pac_shape_tbl[dir - 1][pac_anim & 3], PAL_YELLOW,
            pac_x, pac_y, pac_flags_tbl[dir]);
  }

  for (i = 0; i < GHOST_N; i++) {
    g = &ghosts[i];
    fl = 0;
    dir = g->dir;
    if (dir > 4) dir = DIR_RIGHT;

    if (freeze_ticks && i == freeze_ghost &&
        (g->mode == MODE_EYES || g->mode == MODE_ENTER)) {
      sh = score_spr[freeze_score < 4 ? freeze_score : 3];
      pal = PAL_GHOST_SCORE;
    } else if (g->mode == MODE_EYES || g->mode == MODE_ENTER) {
      sh = (byte)(ghost_dir_base[dir] + anim);
      pal = PAL_EYES;
    } else if (g->mode == MODE_FRIGHT) {
      sh = (byte)(SP_SCARED0 + anim);
      pal = (power_ticks < 60 && (anim_ticks & 0x10))
            ? PAL_SCARED_BLINK : PAL_SCARED;
    } else {
      sh = (byte)(ghost_dir_base[dir] + anim);
      pal = g->color;
    }
    draw_at((byte)(i + 1), sh, pal, g->x, g->y, fl);
  }

  if (fruit_visible) {
    byte fruit = level;
    if (fruit > 7) fruit = 7;
    draw_at(5, (byte)(SP_FRUIT0 + fruit), PAL_FRUIT,
            (word)(FRUIT_TX * 8 + 4), (word)(FRUIT_TY * 8 + 4), 0);
    fruit_visible--;
    fruit_shown = 1;
  } else if (fruit_shown) {
    hide_sprite(5);
    fruit_shown = 0;
  }
}

byte check_ghost_hits(void) {
  byte i;
  byte ptx = tile_x(pac_x);
  byte pty = tile_y(pac_y);
  static const word eat_pts[4] = { 20, 40, 80, 160 };

  for (i = 0; i < GHOST_N; i++) {
    Ghost* g = &ghosts[i];
    if (g->mode == MODE_HOUSE || g->mode == MODE_LEAVE ||
        g->mode == MODE_EYES || g->mode == MODE_ENTER)
      continue;
    if (tile_x(g->x) == ptx && tile_y(g->y) == pty) {
      if (g->mode == MODE_FRIGHT) {
        play_sfx(3);
        if (eat_combo > 3) eat_combo = 3;
        score += eat_pts[eat_combo];
        freeze_score = eat_combo;
        freeze_ghost = i;
        freeze_ticks = EAT_FREEZE_TICKS;
        eat_combo++;
        g->mode = MODE_EYES;
        return 0; /* freeze handled by game loop; don't also die */
      } else if (g->mode == MODE_CHASE || g->mode == MODE_SCATTER) {
        return 1;
      }
    }
  }

  if (fruit_visible) {
    byte ftx = tile_x((word)(pac_x + 4));
    byte fty = tile_y(pac_y);
    if (ftx == FRUIT_TX && fty == FRUIT_TY) {
      score += 10;
      fruit_visible = 0;
      fruit_ticks = 90; /* ambient owns CH2_FRUIT bit */
    }
  }
  return 0;
}

/* Advance 8.8 accumulator; return whole pixels to step this frame. */
byte take_steps(word* frac, word speed) {
  word sum = (word)(*frac + speed);
  *frac = (word)(sum & 0xff);
  return (byte)(sum >> 8);
}

void pac_update(void) {
  byte tx, ty, steps, s;

  if (LEFT1) pac_want = DIR_LEFT;
  if (RIGHT1) pac_want = DIR_RIGHT;
  if (UP1) pac_want = DIR_UP;
  if (DOWN1) pac_want = DIR_DOWN;

  /* Dot/energizer pause: skip movement only this/these frames, then
   * resume full pac_speed_fp() — ghosts keep moving meanwhile. */
  if (pac_stop) {
    pac_stop--;
    return;
  }

  steps = take_steps(&pac_frac, pac_speed_fp());
  for (s = 0; s < steps; s++) {
    /* Cornering / reverse: only probe when player asks for a new dir.
     * can_move itself skips VRAM peeks until leave-tile mid. */
    if (pac_want != pac_dir && can_move(pac_x, pac_y, pac_want, 1))
      pac_dir = pac_want;
    if (!can_move(pac_x, pac_y, pac_dir, 1))
      break;
    move_pos(&pac_x, &pac_y, pac_dir, 1);
    pac_anim++; /* mouth cycles with travel, freezes when blocked/paused */
  }

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

  for (i = 0; i < GHOST_N; i++) {
    update_ghost_state(i);
    g = &ghosts[i];
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

void release_ghosts(void) { }
