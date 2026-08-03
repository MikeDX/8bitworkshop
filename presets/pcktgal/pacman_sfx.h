/*
 * Namco WSG helpers — same channel bits as pakupaku / arcade ROM driver.
 */
#ifndef PACMAN_SFX_H
#define PACMAN_SFX_H

#include "pacman_game.h"

#define CH1_E_NUM    (*(volatile byte*)0x4e9c)
#define CH2_E_NUM    (*(volatile byte*)0x4eac)
#define CH3_E_NUM    (*(volatile byte*)0x4ebc)
#define CH1_W_NUM    (*(volatile byte*)0x4ecc)
#define CH2_W_NUM    (*(volatile byte*)0x4edc)
#define CH3_W_NUM    (*(volatile byte*)0x4eec)

#define CH2_SIRENS   0x1f
/* CH2 effect bits: bit5 = eat-fruit, bit6 = eyes-return whoop. */
#define CH2_FRUIT    0x20
#define CH2_RETREAT  0x40
#define CH2_WEEOOH   0x01
#define CH3_POWER    0x04

void sfx_off(void);
void tick_fright(void);
void start_fright(void);
void stop_fright(void);
void play_prelude(void);
void play_sfx(byte id);
void update_ambient(void);
byte any_eyes(void);

#endif
