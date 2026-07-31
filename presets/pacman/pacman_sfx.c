#pragma opt_code_speed
#include "pacman_sfx.h"

byte any_eyes(void) {
  byte i;
  for (i = 0; i < GHOST_N; i++)
    if (ghosts[i].mode == MODE_EYES || ghosts[i].mode == MODE_ENTER) return 1;
  return 0;
}

void sfx_off(void) {
  volatile byte* p;
  fruit_ticks = 0;
  fright_on = 0;
  fright_tick = 0;
  fright_freq = 0;
  for (p = (volatile byte*)0x4e8c; p < (volatile byte*)0x4efc; p++)
    *p = 0;
  sound_off();
}

void tick_fright(void) {
  if (any_eyes() || !power_ticks) {
    fright_on = 0;
    fright_tick = 0;
    fright_freq = 0;
    return;
  }
  if (fright_tick == 0)
    fright_freq = 0x0180;
  else if ((fright_tick & 7) == 0)
    fright_freq = 0x0180;
  else
    fright_freq += 0x0180;
  fright_tick++;
  sound_voice(1, fright_freq, 10, 4);
}

void start_fright(void) {
  if (any_eyes()) return;
  CH2_E_NUM &= (byte)~(CH2_SIRENS | CH2_RETREAT | CH2_FRUIT);
  fruit_ticks = 0;
  fright_on = 1;
  fright_tick = 0;
  fright_freq = 0x0180;
  sound_voice(1, fright_freq, 10, 4);
}

void stop_fright(void) {
  if (!fright_on) return;
  fright_on = 0;
  fright_tick = 0;
  fright_freq = 0;
  sound_voice(1, 0, 0, 0);
}

void play_prelude(void) {
  sfx_off();
  CH1_W_NUM = 2;
  CH2_W_NUM = 2;
}

void play_sfx(byte id) {
  if (id && (CH1_W_NUM | CH2_W_NUM)) {
    CH1_W_NUM = CH2_W_NUM = 0;
  }
  switch (id) {
  case 1:
    CH3_E_NUM = (byte)((CH3_E_NUM & ~0x02) | 0x01);
    break;
  case 2:
    CH3_E_NUM = (byte)((CH3_E_NUM & ~0x01) | 0x02);
    break;
  case 3:
    stop_fright();
    CH3_E_NUM |= 0x08;
    break;
  case 4:
    stop_fright();
    fruit_ticks = 0;
    CH2_E_NUM = 0;
    CH1_W_NUM = CH2_W_NUM = 0;
    CH3_E_NUM = 0x10;
    break;
  case 6:
    if (!any_eyes()) start_fright();
    CH3_E_NUM |= CH3_POWER;
    break;
  default:
    sfx_off();
    break;
  }
}

void update_ambient(void) {
  byte want, keep;

  /* Eyes-return whoop owns CH2 — do not mix with fruit. */
  if (any_eyes()) {
    stop_fright();
    fruit_ticks = 0;
    want = CH2_RETREAT;
  } else if (power_ticks) {
    if (!fright_on) start_fright();
    want = 0;
  } else {
    stop_fright();
    want = CH2_WEEOOH;
  }

  keep = (byte)(CH2_E_NUM & (byte)~(CH2_SIRENS | CH2_RETREAT | CH2_FRUIT));
  if (fruit_ticks && !any_eyes()) {
    fruit_ticks--;
    if (fruit_ticks)
      keep |= CH2_FRUIT;
  }
  CH2_E_NUM = (byte)(keep | want);
}
