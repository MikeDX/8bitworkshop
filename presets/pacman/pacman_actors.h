#ifndef PACMAN_ACTORS_H
#define PACMAN_ACTORS_H

#include "pacman_game.h"

void actors_reset_level(void);
void pac_update(void);
void ghosts_update(void);
void actors_draw(void);
byte check_ghost_hits(void); /* 1 = pac died */
void release_ghosts(void);

/* Used by pacman_render.c (low CODE) */
byte tile_x(word px);
byte tile_y(word py);
byte can_move(word px, word py, byte dir, byte cornering);
void move_pos(word* px, word* py, byte dir, byte cornering);
word pac_speed_fp(void);
word ghost_speed_fp(Ghost* g);
word ghost_speed_cached(Ghost* g);
byte take_steps(word* frac, word speed);
byte update_ghost_dir(byte i);
void update_ghost_state(byte i);
void set_house_limits(void);


#endif
