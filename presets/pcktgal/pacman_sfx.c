#include "pacman_sfx.h"
#include "pacman_common.h"

void sfx_off(void) {}
void tick_fright(void) {}
void start_fright(void) {}
void stop_fright(void) {}
void play_prelude(void) {}
void play_sfx(byte id) { (void)id; }
void update_ambient(void) {}

byte any_eyes(void) {
  extern byte eyes_present;
  return eyes_present;
}
