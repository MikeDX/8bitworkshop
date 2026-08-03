/*
 * Pocket Gal sound — latch commands to YM2203 audio CPU (Namco WSG-ish).
 * Audio firmware: presets/pcktgal/audio.s → eb03-2.f2
 */
#include <peekpoke.h>
#include "pacman_sfx.h"
#include "pacman_common.h"

/* Must match audio_cmds.inc */
#define CMD_OFF         0
#define CMD_WAKA1       1
#define CMD_WAKA2       2
#define CMD_EAT         3
#define CMD_DEATH       4
#define CMD_COIN        5
#define CMD_POWER       6
#define CMD_FRIGHT_OFF  7
#define CMD_SIREN       8
#define CMD_EYES        9
#define CMD_FRUIT       10
#define CMD_PRELUDE       11
#define CMD_INTERMISSION  12

static byte last_ambient;
static byte sfx_fright;

static void sound_cmd(byte c) {
  POKE(0x1a00, c);
}

byte any_eyes(void) {
  return eyes_present;
}

void sfx_off(void) {
  sfx_fright = 0;
  last_ambient = 0xff;
  fruit_ticks = 0;
  sound_cmd(CMD_OFF);
}

void tick_fright(void) {
  /* Audio CPU warbles on its own while fright is active. */
  if (any_eyes() || !power_ticks) {
    if (sfx_fright) {
      sfx_fright = 0;
      sound_cmd(CMD_FRIGHT_OFF);
    }
  }
}

void start_fright(void) {
  if (any_eyes()) return;
  sfx_fright = 1;
  sound_cmd(CMD_POWER);
}

void stop_fright(void) {
  if (!sfx_fright) return;
  sfx_fright = 0;
  sound_cmd(CMD_FRIGHT_OFF);
}

void play_prelude(void) {
  if (attract_demo) return;
  sfx_off();
  sound_cmd(CMD_PRELUDE);
}

void play_intermission(void) {
  if (attract_demo) return;
  sfx_off();
  sound_cmd(CMD_INTERMISSION);
}

void play_sfx(byte id) {
  if (attract_demo && id != 5) return;
  switch (id) {
  case 1: sound_cmd(CMD_WAKA1); break;
  case 2: sound_cmd(CMD_WAKA2); break;
  case 3:
    stop_fright();
    sound_cmd(CMD_EAT);
    break;
  case 4:
    stop_fright();
    last_ambient = 0xff;
    sound_cmd(CMD_DEATH);
    break;
  case 5:
    sound_cmd(CMD_COIN);
    break;
  case 6:
    if (!any_eyes()) start_fright();
    break;
  default:
    sfx_off();
    break;
  }
}

void update_ambient(void) {
  byte want;

  if (attract_demo) return;

  if (any_eyes()) {
    stop_fright();
    want = CMD_EYES;
  } else if (power_ticks) {
    if (!sfx_fright) start_fright();
    want = CMD_POWER; /* keep fright; no siren */
  } else {
    stop_fright();
    want = CMD_SIREN;
  }

  if (fruit_ticks && !any_eyes()) {
    fruit_ticks--;
    if (fruit_ticks && !(fruit_ticks & 7))
      sound_cmd(CMD_FRUIT);
  }

  if (want != last_ambient) {
    last_ambient = want;
    if (want != CMD_POWER)
      sound_cmd(want);
  }
}
