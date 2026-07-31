/*
 * Pac-Man — C clone on Pac-Man hardware (Namco gfx + ROM sound driver).
 *
 * Regenerate assets/sound: python3 scripts/gen_pacman_assets.py
 * Sound engine is relocatable ASM in pacman_sound.c (NMI calls symbols).
 *
 * Controls: D-pad move, Start from title / after death.
 */
//#link "pacman_common.c"
//#link "pacman_assets.c"
//#link "pacman_sound.c"
//#link "pacman_sfx.c"
//#link "pacman_maze.c"
//#link "pacman_actors.c"
//#link "pacman_render.c"

#include "pacman_common.h"
#include "pacman_assets.h"
#include "pacman_game.h"
#include "pacman_sfx.h"
#include "pacman_maze.h"
#include "pacman_actors.h"

/* ---- globals ---- */
word rnd = 0xCACE;
word score;
word power_ticks;
word anim_ticks;
word round_ticks;
word fright_freq;
word fruit_ticks;
word fruit_visible;
word force_house;
word freeze_ticks;
word pac_x, pac_y;
word pac_frac;

byte lives;
byte level;
byte dots_left;
byte dots_eaten;
byte game_over;
byte waka;
byte fright_on;
byte fright_tick;
byte eat_combo;
byte elroy;
byte freeze_ghost;
byte freeze_score;
byte pac_dir, pac_want;
byte pac_dead;
byte tick;
byte pac_stop;
byte global_dot_mode;
byte global_dot_counter;

Ghost ghosts[GHOST_N];

void start(void) __naked {
__asm
        jp      real_start

        .ds     0x0010 - (. - _start)
        ; RST 10/18/20 from pacman.6e
        .db 0x85,0x6f,0x3e,0x00,0x8c,0x67,0x7e,0xc9,0x78,0x87,0xd7,0x5f,0x23,0x56,0xeb,0xc9
        .db 0xe1,0x87,0xd7,0x5f,0x23,0x56,0xeb,0xe9
        .ds     0x0066 - (. - _start)

        push    af
        push    bc
        push    de
        push    hl
        push    ix
        push    iy

        ld      hl, #0x4e8c
        ld      de, #0x5050
        ld      bc, #0x0010
        ldir

        ld      a, (0x4ecc)
        and     a
        ld      a, (0x4ecf)
        jr      nz, 00010$
        ld      a, (0x4e9f)
00010$:
        ld      (0x5045), a
        ld      a, (0x4edc)
        and     a
        ld      a, (0x4edf)
        jr      nz, 00011$
        ld      a, (0x4eaf)
00011$:
        ld      (0x504a), a
        ld      a, (0x4eec)
        and     a
        ld      a, (0x4eef)
        jr      nz, 00012$
        ld      a, (0x4ebf)
00012$:
        ld      (0x504f), a

        call    _pac_sound_effects
        call    _pac_sound_engine

        ld      a, (_fright_on)
        or      a
        call    nz, _tick_fright

        ld      hl, #0x4c84
        inc     (hl)

        ld      a, (_video_framecount)
        inc     a
        ld      (_video_framecount), a

        pop     iy
        pop     ix
        pop     hl
        pop     de
        pop     bc
        pop     af
        retn

real_start:
        ld      sp, #0x4fc0
        ld      bc, #l__INITIALIZER
        ld      a, b
        or      a, c
        jr      z, 00001$
        ld      de, #s__INITIALIZED
        ld      hl, #s__INITIALIZER
        ldir
00001$:
        ld      hl, #0x4e8c
        ld      de, #0x4e8d
        ld      (hl), #0
        ld      bc, #0x006f
        ldir
        xor     a
        ld      (0x4c84), a
        jp      _main
__endasm;
}

static word hud_score;
static byte hud_lives, hud_level;
static byte hud_ready; /* 0 = force full chrome + digits */

/* Decimal digits without Z80 16-bit / and % (those blow the frame budget). */
static void word_to_4digits(word n, byte* d) {
  d[0] = 0;
  while (n >= 1000) { n = (word)(n - 1000); d[0]++; }
  d[1] = 0;
  while (n >= 100) { n = (word)(n - 100); d[1]++; }
  d[2] = 0;
  while (n >= 10) { n = (word)(n - 10); d[2]++; }
  d[3] = (byte)n;
}

void draw_hud(void) {
  byte dig[4], old[4];
  byte i;
  byte force = (byte)(hud_ready == 0);

  if (!force && score == hud_score && lives == hud_lives && level == hud_level)
    return;

  if (force) {
    put_string(1, 0, "1UP", PAL_CYAN);
    put_string(12, 0, "L", PAL_YELLOW);
    put_string(1, 34, "LIVES", PAL_CYAN);
  }

  if (force || score != hud_score) {
    word_to_4digits(score, dig);
    word_to_4digits(force ? 0 : hud_score, old);
    for (i = 0; i < 4; i++) {
      if (force || dig[i] != old[i])
        put_digit((byte)(5 + i), 0, dig[i], PAL_WHITE);
    }
    hud_score = score;
  }

  if (force || lives != hud_lives) {
    put_digit(7, 34, lives > 9 ? 9 : lives, PAL_YELLOW);
    hud_lives = lives;
  }

  if (force || level != hud_level) {
    put_digit(13, 0, (byte)((level + 1) % 10), PAL_YELLOW);
    hud_level = level;
  }

  hud_ready = 1;
}

void title_screen(void) {
  clrscr(0);
  hide_all_sprites();
  put_string(10, 8, "PAC-MAN", PAL_YELLOW);
  put_string(6, 12, "C ON NAMCO HW", PAL_CYAN);
  put_string(6, 30, "PRESS START", PAL_WHITE);
  set_sprite_ex(0, SP_OPEN1, PAL_YELLOW, 72, 160, 0);
  set_sprite_ex(1, SP_GHOST_R0, PAL_RED, 100, 160, 0);
  set_sprite_ex(2, SP_GHOST_R0, PAL_PINK, 120, 160, 0);
  set_sprite_ex(3, SP_GHOST_R0, PAL_CYAN, 140, 160, 0);
  set_sprite_ex(4, SP_GHOST_R0, PAL_ORANGE, 160, 160, 0);
  play_prelude();
  while (!(START1 || FIRE1)) {
    wait_vblank();
    watchdog = 0;
  }
  while (START1 || FIRE1) {
    wait_vblank();
    watchdog = 0;
  }
  sfx_off();
}

void show_death(void) {
  byte t, frame;
  byte i;
  for (i = 1; i < 8; i++) hide_sprite(i);
  play_sfx(4);
  for (t = 0; t < 88; t++) {
    frame = (byte)(SP_DEATH0 + (t / 8));
    if (frame > SP_DEATH_LAST) frame = SP_DEATH_LAST;
    set_sprite_ex(0, frame, PAL_YELLOW, (byte)(pac_x - 8), (byte)(pac_y - 8), 0);
    wait_vblank();
    watchdog = 0;
    if (t == 72) CH3_E_NUM = 0x20;
  }
  hide_sprite(0);
  while (CH3_E_NUM & 0x20) {
    wait_vblank();
    watchdog = 0;
  }
  wait_vblank();
  CH3_E_NUM = 0x20;
  while (CH3_E_NUM & 0x20) {
    wait_vblank();
    watchdog = 0;
  }
  sfx_off();
}

void show_game_over(void) {
  put_string(9, 16, "GAME OVER", PAL_RED);
  put_string(6, 18, "PRESS START", PAL_WHITE);
  while (!(START1 || FIRE1)) {
    wait_vblank();
    watchdog = 0;
  }
  while (START1 || FIRE1) {
    wait_vblank();
    watchdog = 0;
  }
}

void start_round(void) {
  hide_all_sprites();
  draw_maze();
  count_dots();
  actors_reset_level();
  hud_ready = 0; /* force HUD chrome + digits */
  draw_hud();
  sfx_off();
  update_ambient();
  /* brief ready pause */
  {
    byte t;
    put_string(11, 16, "READY", PAL_YELLOW);
    for (t = 0; t < 90; t++) {
      wait_vblank();
      watchdog = 0;
      actors_draw();
    }
    put_string(11, 16, "     ", 0);
  }
}

void next_level(void) {
  byte t;
  sfx_off();
  for (t = 0; t < 60; t++) {
    wait_vblank();
    watchdog = 0;
  }
  if (level < 255) level++;
  global_dot_mode = 0;
  global_dot_counter = 0;
  start_round();
}

void game_loop(void) {
  lives = 3;
  level = 0;
  score = 0;
  game_over = 0;
  global_dot_mode = 0;
  global_dot_counter = 0;
  clrscr(0);
  start_round();

  while (!game_over) {
    wait_vblank();
    watchdog = 0;
    anim_ticks++;
    tick++;

    if (freeze_ticks) {
      freeze_ticks--;
      /* timers still advance during eat-freeze (arcade-like) */
      if (power_ticks) power_ticks--;
      update_ambient();
      actors_draw();
      continue;
    }

    pac_update();
    ghosts_update();

    if (check_ghost_hits()) {
      show_death();
      if (lives) lives--;
      if (!lives) {
        game_over = 1;
        break;
      }
      /* After a death, use global dot counter for house release (floooh) */
      global_dot_mode = 1;
      global_dot_counter = 0;
      actors_reset_level();
      draw_hud();
      update_ambient();
    }

    if (power_ticks) power_ticks--;
    update_ambient();
    draw_hud();
    actors_draw();

    if (!dots_left)
      next_level();
  }
}

void main(void) {
  interrupt_enable = 1;
  sound_enable = 1;
  flip_screen = 0;
  watchdog = 0;
  video_framecount = 0;

  while (1) {
    title_screen();
    game_loop();
    show_game_over();
  }
}
