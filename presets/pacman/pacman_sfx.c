#pragma opt_code_speed
#include "pacman_sfx.h"
#include "pacman_assets.h"

static void pac_sound_vblank(void);

byte any_eyes(void) {
  return eyes_present;
}

void sfx_off(void) {
  volatile byte* p;
  /* Keep Namco engine armed whenever SFX runs (survives stale main / re-entry). */
  pac_vblank_hook = pac_sound_vblank;
  fruit_ticks = 0;
  fright_on = 0;
  fright_tick = 0;
  fright_freq = 0;
  for (p = (volatile byte*)0x4e8c; p < (volatile byte*)0x4efc; p++)
    *p = 0;
  sound_off();
}

/* VBLANK: ROM effects + voice engine + continuous fright warble. */
static void pac_sound_vblank(void) {
  pac_sound_effects();
  pac_sound_engine();
  if (fright_on)
    tick_fright();
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
  /* CH1/CH2 each have a 2-entry song table: bit0=start, bit1=intermission. */
  if (attract_demo) return;
  sfx_off();
  CH1_W_NUM = 1;
  CH2_W_NUM = 1;
}

void play_sfx(byte id) {
  /* Attract chase is silent; coin ding uses id 5 while attract_demo==0. */
  if (attract_demo) return;
  pac_vblank_hook = pac_sound_vblank;
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
  case 5:
    /* Coin / credit ding — arcade sets CH1_E_NUM bit1 */
    CH1_E_NUM |= 0x02;
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

  if (attract_demo) return;

  pac_vblank_hook = pac_sound_vblank;

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
