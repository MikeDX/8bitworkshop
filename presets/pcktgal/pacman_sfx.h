/*
 * Pocket Gal sound — latch commands to YM2203 audio CPU.
 * Commands: presets/pcktgal/audio_cmds.inc
 */
#ifndef PACMAN_SFX_H
#define PACMAN_SFX_H

#include "pacman_game.h"

void sfx_off(void);
void tick_fright(void);
void start_fright(void);
void stop_fright(void);
void play_prelude(void);
void play_intermission(void);
void play_sfx(byte id);
void update_ambient(void);
byte any_eyes(void);

#endif
