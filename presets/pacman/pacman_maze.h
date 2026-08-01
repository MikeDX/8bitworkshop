#ifndef PACMAN_MAZE_H
#define PACMAN_MAZE_H

#include "pacman_game.h"

void draw_maze(void);
void count_dots(void);
void try_eat_tile(byte tx, byte ty);
byte tile_blocked(byte tx, byte ty);
byte peek_maze(byte tx, byte ty);
void poke_maze(byte tx, byte ty, byte tile, byte pal);
word fright_duration(void);
word fright_flash_ticks(void);
byte maze_tile(byte tx, byte ty);

#endif
