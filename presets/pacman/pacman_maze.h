#ifndef PACMAN_MAZE_H
#define PACMAN_MAZE_H

#include "pacman_game.h"

void draw_maze(void);
void count_dots(void);
void try_eat_tile(byte tx, byte ty);
byte tile_blocked(byte tx, byte ty);
word fright_duration(void);
byte maze_tile(byte tx, byte ty);

#endif
