#pragma opt_code_speed
#include "pacman_actors.h"
#include "pacman_maze.h"
#include "pacman_sfx.h"
#include "pacman_assets.h"

extern const byte ghost_pal[GHOST_N];

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
/*
 * Reset Pac + ghosts for a new maze or after death.
 * after_death=1: [CONFIRM] keep personal dot counters; caller sets global_dot_mode
 *                and elroy_suspended (Elroy stays off until Clyde leaves).
 * after_death=0: [CONFIRM] clear personal counters; Elroy may activate normally.
 */
void actors_reset_level(byte after_death) {
  byte i;

  pac_tx = 14; pac_ox = 0;
  pac_ty = 26; pac_oy = 4;
  pac_frac = 0;
  pac_dir = DIR_LEFT;
  pac_want = DIR_LEFT;
  pac_dead = 0;
  pac_eat_tx = 0xff;
  pac_eat_ty = 0xff;
  pac_anim = 0;
  power_ticks = 0;
  eat_combo = 0;
  round_ticks = 0; /* [CONFIRM] S/C schedule restarts each life/round */
  elroy = 0;
  if (!after_death) elroy_suspended = 0;
  fruit_ticks = 0;
  fruit_visible = 0;
  fruit_shown = 0;
  pac_stop = 0;
  force_house = 0; /* [CONFIRM] house force-exit timer resets */
  freeze_ticks = 0;
  eyes_present = 0;
  rnd = 0xCACE; /* [CONFIRM] PRNG reseeds each life/round (frightened paths) */

  for (i = 0; i < GHOST_N; i++) {
    Ghost* g = &ghosts[i];
    g->color = ghost_pal[i];
    /* scat corners live in scat_x[]/scat_y[] — not cached on Ghost */
    if (!after_death) g->dot_counter = 0;
    g->frac = 0;
    g->speed_sig = 0xff;
    g->tx = 14; g->ox = 0;
    g->ty = 17; g->oy = 4;
    g->dir = DIR_UP;
    g->next_dir = DIR_UP;
    g->mode = MODE_HOUSE;
    g->frightened = 0;
  }
  set_house_limits();

  /* Blinky outside (extras may shift him left on the ante row). */
  ghosts[0].ty = 14; ghosts[0].oy = 4;
  ghosts[0].dir = DIR_LEFT;
  ghosts[0].next_dir = DIR_LEFT;
  ghosts[0].mode = MODE_SCATTER;
  ghosts[0].frightened = 0;

#if defined(ENABLE_ZOMBIE)
  /* 3 above house over Inky/Pinky/Clyde columns. */
  ghosts[0].tx = 12; ghosts[0].ox = 0;
  ghosts[4].tx = 14; ghosts[4].ox = 0;
  ghosts[4].ty = 14; ghosts[4].oy = 4;
  ghosts[4].dir = DIR_LEFT;
  ghosts[4].next_dir = DIR_LEFT;
  ghosts[4].mode = MODE_SCATTER;
  ghosts[5].tx = 16; ghosts[5].ox = 0;
  ghosts[5].ty = 14; ghosts[5].oy = 4;
  ghosts[5].dir = DIR_LEFT;
  ghosts[5].next_dir = DIR_LEFT;
  ghosts[5].mode = MODE_SCATTER;
#elif defined(ENABLE_CURLY)
  /* Curly alone: Blinky left, Curly in classic Blinky slot. */
  ghosts[0].tx = 12; ghosts[0].ox = 0;
  ghosts[4].tx = 14; ghosts[4].ox = 0;
  ghosts[4].ty = 14; ghosts[4].oy = 4;
  ghosts[4].dir = DIR_LEFT;
  ghosts[4].next_dir = DIR_LEFT;
  ghosts[4].mode = MODE_SCATTER;
#endif

  /* Pinky middle */
  ghosts[1].dir = DIR_DOWN;
  ghosts[1].next_dir = DIR_DOWN;

  /* Inky left / Clyde right */
  ghosts[2].tx = 12; ghosts[2].ox = 0;
  ghosts[3].tx = 16; ghosts[3].ox = 0;
}

#pragma opt_code_speed
byte check_ghost_hits(void) {
  byte i;
  Ghost* g = ghosts;

  for (i = 0; i < GHOST_N; i++, g++) {
    byte mode = g->mode;
    if (mode >= MODE_EYES)
      continue;
    if (g->tx != pac_tx || g->ty != pac_ty)
      continue;
    if (mode == MODE_FRIGHT || g->frightened) {
      static const byte eat_pts[4] = { 20, 40, 80, 160 };
      byte combo = eat_combo;
      if (combo > 3) combo = 3;
      play_sfx(3);
      if (!attract_demo)
        score += eat_pts[combo];
      freeze_score = combo;
      freeze_ghost = i;
      freeze_ticks = EAT_FREEZE_TICKS;
      eat_combo = (byte)(combo + 1);
      g->mode = MODE_EYES;
      g->frightened = 0; /* eaten — no longer vulnerable */
      eyes_present = 1;
      return 0;
    }
    return 1;
  }

  if (fruit_visible && pac_tx == FRUIT_TX && pac_ty == FRUIT_TY) {
    /* A.1 bonus /100 (key=50); score is tens → ×10. 500 tens won't fit in a byte. */
    static const byte fruit_pts[13] = {
      1, 3, 5, 5, 7, 7, 10, 10, 20, 20, 30, 30, 50
    };
    byte lv = level;
    if (lv > 12) lv = 12;
    if (!attract_demo)
      score += (word)fruit_pts[lv] * 10;
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

/* Sprite from tile+offset center: cx=tx*8+ox → reg 247-cx.
 * Macro (not a call) — draw is ~20% of useful work. */
#define PAC_DRAW(i, shape, pal, tx, ty, ox, oy, fl) do { \
  byte _cx = (byte)(((byte)(tx) << 3) + (byte)(ox)); \
  byte _cy = (byte)(((byte)(ty) << 3) + (byte)(oy)); \
  byte* _a = (byte*)(0x4ff0 + ((byte)(i) << 1)); \
  byte* _p = (byte*)(0x5060 + ((byte)(i) << 1)); \
  _a[0] = (byte)(((byte)(shape) << 2) | ((byte)(fl) & 3)); \
  _a[1] = (byte)(pal); \
  _p[0] = (byte)(247 - _cx); \
  _p[1] = (byte)(280 - _cy); \
} while (0)

void actors_draw_anim(byte animate) {
  byte i, sh, pal, anim, dir;
  byte eyes_pal;
  word flash_at;
  Ghost* g;

  /* Freeze sprite frames during score popup (Pac hidden, ghosts still). */
  if (freeze_ticks) animate = 0;
  anim = animate ? (byte)((anim_ticks >> 2) & 1) : 0;
      eyes_pal = attract_corridor ? 0 : PAL_EYES;
  flash_at = fright_flash_ticks();

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
    PAC_DRAW(0, sh, PAL_YELLOW, pac_tx, pac_ty, pac_ox, pac_oy,
             pac_flags_tbl[dir]);
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
    } else if (g->frightened) {
      sh = (byte)(SP_SCARED0 + anim);
      /* No frame anim or white/blue flash during ghost-eat score freeze */
      pal = (!freeze_ticks && power_ticks <= flash_at && (anim_ticks & 0x10))
            ? PAL_SCARED_BLINK : PAL_SCARED;
    } else {
      sh = (byte)(ghost_dir_base[dir] + anim);
      pal = g->color;
    }
    PAC_DRAW((byte)(i + 1), sh, pal, g->tx, g->ty, g->ox, g->oy, 0);
  }

  if (fruit_visible) {
    byte fruit = level;
    if (fruit > 7) fruit = 7;
    PAC_DRAW(SPR_FRUIT, (byte)(SP_FRUIT0 + fruit), PAL_FRUIT,
             FRUIT_TX, FRUIT_TY, 4, 4, 0);
    fruit_visible--;
    fruit_shown = 1;
  } else if (fruit_shown) {
    hide_sprite(SPR_FRUIT);
    fruit_shown = 0;
  }

#if GHOST_AI_DEBUG
  draw_ghost_ai_debug();
#endif
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
  byte steps, s, moved;

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

  /* Title chase corridor: X-only, no tunnel wrap (off-screen spawn). */
  if (attract_corridor) {
    if (pac_want != pac_dir)
      pac_dir = pac_want;
    moved = 0;
    steps = take_steps(&pac_frac, pac_speed_fp());
    for (s = 0; s < steps; s++) {
      if (pac_dir == DIR_LEFT) {
        if (pac_ox) pac_ox--;
        else if (pac_tx) { pac_tx--; pac_ox = 7; }
        else break;
      } else if (pac_dir == DIR_RIGHT) {
        pac_ox++;
        if (pac_ox >= 8) { pac_ox = 0; pac_tx++; }
      } else
        break;
      moved = 1;
    }
    pac_ty = 20; pac_oy = 4;
    if (moved)
      pac_anim = (byte)((pac_anim + 1) & 3);
    if (pac_tx >= 1 && pac_tx < 28) {
      if (pac_tx != pac_eat_tx || pac_ty != pac_eat_ty) {
        pac_eat_tx = pac_tx;
        pac_eat_ty = pac_ty;
        try_eat_tile(pac_tx, pac_ty);
      }
    }
    return;
  }

  moved = 0;
  steps = take_steps(&pac_frac, pac_speed_fp());
  for (s = 0; s < steps; s++) {
    /* Cornering / reverse: only probe when player asks for a new dir.
     * can_move itself skips VRAM peeks until leave-tile mid. */
    if (pac_want != pac_dir &&
        can_move(&pac_tx, pac_want, 1))
      pac_dir = pac_want;
    if (!can_move(&pac_tx, pac_dir, 1))
      break;
    move_pos_pac(&pac_tx, pac_dir);
    moved = 1;
  }
  /* Simple rule: moved → tick (clamped 0..3). stopped → freeze. */
  if (moved)
    pac_anim = (byte)((pac_anim + 1) & 3);

  if (pac_tx != pac_eat_tx || pac_ty != pac_eat_ty) {
    pac_eat_tx = pac_tx;
    pac_eat_ty = pac_ty;
    try_eat_tile(pac_tx, pac_ty);
  }
}

void ghosts_update(void) {
  byte i, steps, s;
  Ghost* g;
  word force_limit;

  if (!power_ticks)
    round_ticks++; /* [CONFIRM] S/C timer paused during fright */
  force_house++;

  /* [CHANGE] 4s L1–4, 3s L5+; release only most-preferred ghost in house */
  force_limit = (level < 4) ? (word)(4 * 60) : (word)(3 * 60);
  if (force_house >= force_limit) {
    force_house = 0;
    for (i = 1; i <= 3; i++) {
      if (ghosts[i].mode == MODE_HOUSE) {
        ghosts[i].mode = MODE_LEAVE;
        if (i == 3 && elroy_suspended) {
          elroy_suspended = 0;
          update_elroy();
        }
        break;
      }
    }
  }

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
        move_pos(&g->tx, g->dir);
      } else if (AT_TILE_MID(g->ox, g->oy)) {
        update_ghost_dir(i);
        if (!can_move(&g->tx, g->dir, 0))
          break;
        move_pos(&g->tx, g->dir);
      } else {
        move_pos(&g->tx, g->dir);
      }
    }
  }
}
