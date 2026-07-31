/*
 * Shared Pac-Man game state — gameplay aligned with floooh/pacman.c
 */
#ifndef PACMAN_GAME_H
#define PACMAN_GAME_H

#include "pacman_common.h"

/* Arcade sprite indices (pacman.5f) — confirmed from hardware ROM */
#define SP_PAC_R_WIDE 0x2c  /* open wide right */
#define SP_PAC_D_WIDE 0x2d  /* open wide down */
#define SP_PAC_R      0x2e  /* open right */
#define SP_PAC_D      0x2f  /* open down */
/* left = right + FLIP_X; up = down + FLIP_Y */
#define SP_CLOSED     0x30
#define SP_DEATH0     0x34
#define SP_DEATH_LAST 0x3e
#define SP_GHOST_R0   0x20
#define SP_GHOST_R1   0x21
#define SP_GHOST_D0   0x22
#define SP_GHOST_D1   0x23
#define SP_GHOST_L0   0x24
#define SP_GHOST_L1   0x25
#define SP_GHOST_U0   0x26
#define SP_GHOST_U1   0x27
#define SP_SCARED0    0x1c
#define SP_SCARED1    0x1d
/* Score pop when a ghost is eaten (NOT eyes — eyes reuse body sprites) */
#define SP_SCORE200   0x28
#define SP_SCORE400   0x29
#define SP_SCORE800   0x2a
#define SP_SCORE1600  0x2b
#define SP_FRUIT0     0x00  /* cherries .. through 0x07 */

/* Compat aliases used by title screen */
#define SP_OPEN1      SP_PAC_R_WIDE
#define SP_OPEN2      SP_PAC_R

#define T_DOT_A       0x10
#define T_POWER_A     0x14
#define T_BLANK_A     0x40
#define T_DOOR        0xCF

#define PAL_SCARED    0x11
#define PAL_SCARED_BLINK 0x12
#define PAL_GHOST_SCORE 0x18
#define PAL_EYES      0x19  /* body sprites, eyes-only via this palette */
#define PAL_MAZE      0x10
#define PAL_DOT       0x10
#define PAL_POWER     0x10
#define PAL_FRUIT     0x14
#define PAL_BLINKY    0x01
#define PAL_PINKY     0x03
#define PAL_INKY      0x05
#define PAL_CLYDE     0x07
#define PAL_DOOR      0x18

#define FLIP_X        2
#define FLIP_Y        1

#define DIR_NONE      0
#define DIR_RIGHT     1
#define DIR_DOWN      2
#define DIR_LEFT      3
#define DIR_UP        4

#define MODE_SCATTER  0
#define MODE_CHASE    1
#define MODE_FRIGHT   2
#define MODE_EYES     3
#define MODE_HOUSE    4
#define MODE_LEAVE    5
#define MODE_ENTER    6

#define GHOST_N       4
#define NUM_DOTS      244
#define EAT_FREEZE_TICKS 60

/* floooh anteportas (house exit point), pixel center */
#define ANTE_X        112  /* 14*8 */
#define ANTE_Y        116  /* 14*8+4 */

#define FRUIT_TX      14
#define FRUIT_TY      20

/* Pixel centers sit on *.4 within a tile when at a decision point. */
#define AT_TILE_MID(px, py) ((((px) & 7) == 4) && (((py) & 7) == 4))

typedef struct {
  word x, y;       /* pixel CENTER */
  word frac;       /* 8.8 movement accumulator */
  word speed_fp;   /* cached 8.8 speed */
  byte dir;
  byte next_dir;
  byte mode;
  byte color;
  byte scat_x, scat_y;
  byte dot_counter;
  byte dot_limit;
  byte in_house;
  byte speed_sig;  /* cache key: mode + tunnel + elroy */
} Ghost;

extern word rnd;
extern word score;
extern word hiscore;
extern word power_ticks;
extern word anim_ticks;
extern word round_ticks;
extern word fright_freq;
extern word fruit_ticks;
extern word fruit_visible;   /* word — 600 frames must not wrap */
extern word force_house;     /* frames since last dot */
extern word freeze_ticks;    /* >0: freeze after eating a ghost */
extern word pac_x, pac_y;
extern word pac_frac;        /* 8.8 movement accumulator */

extern byte lives;
extern byte level;
extern byte credits; /* 0..99 */
extern byte dots_left;
extern byte dots_eaten;
extern byte game_over;
extern byte waka;
extern byte fright_on;
extern byte fright_tick;
extern byte eat_combo;
extern byte elroy;
extern byte freeze_ghost;    /* which ghost shows the score pop */
extern byte freeze_score;    /* 0..3 → 200/400/800/1600 */
extern byte pac_dir, pac_want;
extern byte pac_dead;
extern byte tick;
extern byte pac_stop; /* frames Pac stops after eating */
extern byte global_dot_mode;
extern byte global_dot_counter;

extern Ghost ghosts[GHOST_N];

byte rand8(void);
byte abs_diff(byte a, byte b);
byte opposite_dir(byte d);
void dir_vec(byte d, sbyte* dx, sbyte* dy);

#endif
