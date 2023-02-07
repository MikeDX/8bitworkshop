
#include <conio.h>
#include <atari.h>
#include <stdio.h>
#include <conio.h>
#include <peekpoke.h>
#include <6502.h>

//#link "pokeymusic.ca65"

const char music1[] = {
0x35,0x8a,0x37,0x8a,0x33,0x3f,0x8a,0x30,0x3c,0x94,0x3e,0x32,0x8a,0x3a,0x2e,0x94,0x35,0x29,0x8a,0x37,0x2b,0x8a,0x33,0x27,0x8a,0x30,0x24,0x94,0x32,0x26,0x8a,0x2e,0x22,0x94,0x29,0x1d,0x8a,0x2b,0x1f,0x8a,0x27,0x1b,0x8a,0x24,0x18,0x94,0x1a,0x26,0x8a,0x18,0x24,0x8a,0x17,0x23,0x8a,0x16,0x22,0xa8,0x3a,0x35,0x32,0x94,0x29,0x26,0x22,0x8a,0x2a,0x8a,0x2b,0x1b,0x8a,0x33,0x8a,0x22,0x1f,0x27,0x8a,0x2b,0x8a,0x33,0x22,0x16,0x94,0x2b,0x27,0x25,0x8a,0x33,0x8a,0x20,0x14,0x94,0x27,0x24,0x94,0x1f,0x13,0x8a,0x3f,0x37,0x33,0x8a,0x38,0x35,0x27,0x8a,0x39,0x36,0x8a,0x3a,0x37,0x16,0x8a,0x3f,0x37,0x33,0x8a,0x35,0x38,0x22,0x8a,0x3a,0x37,0x8a,0x16,0x8a,0x3e,0x35,0x32,0x8a,0x38,0x35,0x26,0x94,0x3f,0x33,0x37,0x94,0x1f,0x22,0x27,0x94,0x27,0x22,0x1f,0x94,0x29,0x26,0x22,0x8a,0x2a,0x8a,0x2b,0x1b,0x8a,0x33,0x8a,0x1f,0x22,0x27,0x8a,0x2b,0x8a,0x33,0x22,0x16,0x94,0x2b,0x27,0x25,0x8a,0x33,0x8a,0x20,0x14,0x94,0x24,0x27,0x94,0x1f,0x13,0x94,0x3c,0x33,0x30,0x8a,0x3a,0x33,0x2e,0x8a,0x39,0x33,0x2d,0x8a,0x3c,0x30,0x8a,0x3f,0x33,0x1d,0x8a,0x37,0x8a,0x1d,0x8a,0x35,0x8a,0x3f,0x33,0x27,0x8a,0x3c,0x30,0x8a,0x35,0x38,0x26,0x94,0x22,0x16,0x94,0x24,0x18,0x94,0x29,0x26,0x1a,0x8a,0x2a,0x8a,0x2b,0x1b,0x8a,0x33,0x8a,0x27,0x22,0x1f,0x8a,0x2b,0x8a,0x33,0x16,0x22,0x94,0x2b,0x27,0x25,0x8a,0x33,0x8a,0x20,0x14,0x94,0x27,0x24,0x94,0x13,0x1f,0x8a,0x3f,0x33,0x37,0x8a,0x38,0x35,0x27,0x8a,0x39,0x36,0x8a,0x37,0x3a,0x16,0x8a,0x3f,0x37,0x33,0x8a,0x38,0x35,0x27,0x8a,0x3a,0x37,0x8a,0x16,0x8a,0x3e,0x35,0x32,0x8a,0x38,0x35,0x20,0x94,0x3f,0x37,0x33,0x94,0x27,0x22,0x1f,0x94,0x2b,0x27,0x22,0x94,0x3f,0x33,0x8a,0x35,0x8a,0x37,0x27,0x1b,0x8a,0x3f,0x33,0x8a,0x35,0x2b,0x22,0x8a,0x37,0x8a,0x25,0x19,0x8a,0x3f,0x33,0x8a,0x35,0x2b,0x27,0x8a,0x3f,0x33,0x8a,0x37,0x24,0x18,0x8a,0x3f,0x33,0x8a,0x35,0x2c,0x27,0x8a,0x37,0x8a,0x23,0x17,0x8a,0x3f,0x33,0x8a,0x35,0x2c,0x27,0x8a,0x3f,0x33,0x8a,0x3a,0x37,0x22,0x8a,0x3f,0x37,0x33,0x8a,0x38,0x35,0x2b,0x8a,0x3a,0x37,0x8a,0x16,0x8a,0x3e,0x32,0x35,0x8a,0x35,0x38,0x26,0x94,0x3f,0x33,0x37,0x94,0x22,0x16,0x94,0x24,0x18,0x94,0x29,0x26,0x1a,0x8a,0x2a,0x8a,0x2b,0x1b,0x8a,0x33,0x8a,0x27,0x22,0x1f,0x8a,0x2b,0x8a,0x33,0x22,0x16,0x94,0x2b,0x25,0x22,0x8a,0x33,0x8a,0x20,0x14,0x94,0x27,0x24,0x94,0x1f,0x13,0x8a,0x3f,0x37,0x33,0x8a,0x35,0x38,0x27,0x8a,0x39,0x36,0x8a,0x3a,0x37,0x16,0x8a,0x3f,0x37,0x33,0x8a,0x38,0x35,0x27,0x8a,0x3a,0x37,0x8a,0x16,0x8a,0x3e,0x35,0x32,0x8a,0x35,0x38,0x20,0x94,0x37,0x33,0x3f,0x94,0x27,0x22,0x1f,0x94,0x27,0x22,0x1f,0x94,0x29,0x26,0x22,0x8a,0x2a,0x8a,0x2b,0x1b,0x8a,0x33,0x8a,0x22,0x1f,0x27,0x8a,0x2b,0x8a,0x33,0x22,0x16,0x94,0x2b,0x27,0x25,0x8a,0x33,0x8a,0x20,0x14,0x94,0x27,0x24,0x94,0x1f,0x13,0x94,0x3c,0x33,0x30,0x8a,0x3a,0x33,0x2e,0x8a,0x2d,0x33,0x39,0x8a,0x3c,0x30,0x8a,0x3f,0x33,0x27,0x8a,0x37,0x8a,0x1d,0x8a,0x35,0x8a,0x33,0x3f,0x27,0x8a,0x3c,0x30,0x8a,0x38,0x35,0x26,0x94,0x22,0x16,0x94,0x24,0x18,0x94,0x29,0x26,0x1a,0x8a,0x2a,0x8a,0x2b,0x1b,0x8a,0x33,0x8a,0x22,0x27,0x1f,0x8a,0x2b,0x8a,0x33,0x22,0x16,0x94,0x2b,0x27,0x25,0x8a,0x33,0x8a,0x20,0x14,0x94,0x24,0x27,0x94,0x1f,0x13,0x8a,0x3f,0x37,0x33,0x8a,0x38,0x35,0x27,0x8a,0x36,0x39,0x8a,0x3a,0x37,0x16,0x8a,0x3f,0x37,0x33,0x8a,0x38,0x35,0x27,0x8a,0x37,0x3a,0x8a,0x16,0x8a,0x3e,0x35,0x32,0x8a,0x38,0x35,0x22,0x94,0x33,0x37,0x3f,0x94,0x27,0x22,0x1f,0x94,0x22,0x27,0x2b,0x94,0x3f,0x33,0x8a,0x35,0x8a,0x37,0x27,0x1b,0x8a,0x3f,0x33,0x8a,0x35,0x2b,0x27,0x8a,0x37,0x8a,0x25,0x19,0x8a,0x3f,0x33,0x8a,0x35,0x2b,0x22,0x8a,0x3f,0x33,0x8a,0x37,0x24,0x18,0x8a,0x33,0x3f,0x8a,0x35,0x2c,0x27,0x8a,0x37,0x8a,0x23,0x17,0x8a,0x3f,0x33,0x8a,0x35,0x2c,0x27,0x8a,0x3f,0x33,0x8a,0x3a,0x37,0x22,0x8a,0x3f,0x37,0x33,0x8a,0x38,0x35,0x2b,0x8a,0x37,0x3a,0x8a,0x16,0x8a,0x3e,0x35,0x32,0x8a,0x38,0x35,0x22,0x94,0x33,0x37,0x3f,0x94,0x22,0x16,0x94,0x1b,0x0f,0x8a,0x37,0x2b,0x33,0x8a,0x38,0x2c,0x35,0x8a,0x39,0x36,0x2d,0x8a,0x3a,0x37,0x2e,0x94,0x3c,0x37,0x30,0x8a,0x3a,0x2e,0x37,0x8a,0x16,0x8a,0x37,0x33,0x2b,0x8a,0x2c,0x38,0x35,0x8a,0x39,0x36,0x2d,0x8a,0x3a,0x37,0x2e,0x94,0x3c,0x37,0x30,0x8a,0x2e,0x37,0x3a,0x8a,0x16,0x8a,0x37,0x8a,0x33,0x27,0x2b,0x8a,0x2e,0x8a,0x30,0x14,0x8a,0x32,0x8a,0x33,0x2c,0x27,0x8a,0x35,0x8a,0x37,0x20,0x8a,0x35,0x8a,0x33,0x2c,0x27,0x8a,0x35,0x8a,0x2e,0x1f,0x8a,0x37,0x8a,0x38,0x2b,0x27,0x8a,0x3a,0x8a,0x3c,0x16,0x8a,0x3a,0x8a,0x37,0x22,0x2b,0x8a,0x38,0x8a,0x3a,0x37,0x2e,0x94,0x3c,0x30,0x37,0x8a,0x3a,0x37,0x2e,0x8a,0x16,0x8a,0x37,0x33,0x2b,0x8a,0x38,0x2c,0x35,0x8a,0x39,0x36,0x2d,0x8a,0x3a,0x37,0x2e,0x94,0x3c,0x37,0x30,0x8a,0x2e,0x37,0x3a,0x8a,0x1f,0x8a,0x3a,0x8a,0x3c,0x1e,0x8a,0x3d,0x8a,0x3e,0x3a,0x35,0x8a,0x3e,0x35,0x3a,0x8a,0x22,0x29,0x26,0x8a,0x3e,0x33,0x39,0x8a,0x1d,0x8a,0x3c,0x8a,0x39,0x33,0x29,0x8a,0x35,0x8a,0x3a,0x32,0x29,0x94,0x20,0x14,0x94,0x1f,0x13,0x8a,0x37,0x2b,0x33,0x8a,0x38,0x35,0x2c,0x8a,0x39,0x36,0x2d,0x8a,0x2e,0x3a,0x37,0x94,0x3c,0x37,0x30,0x8a,0x3a,0x37,0x2e,0x8a,0x16,0x8a,0x33,0x2b,0x37,0x8a,0x38,0x35,0x2c,0x8a,0x39,0x36,0x2d,0x8a,0x3a,0x37,0x2e,0x94,0x30,0x37,0x3c,0x8a,0x3a,0x2e,0x37,0x8a,0x16,0x8a,0x37,0x8a,0x33,0x2b,0x27,0x8a,0x2e,0x8a,0x30,0x14,0x8a,0x32,0x8a,0x33,0x2c,0x27,0x8a,0x35,0x8a,0x37,0x20,0x8a,0x35,0x8a,0x33,0x23,0x2c,0x8a,0x35,0x8a,0x33,0x1f,0x94,0x2b,0x22,0x27,0x94,0x1b,0x8a,0x2e,0x8a,0x2d,0x2b,0x27,0x8a,0x2e,0x8a,0x33,0x2c,0x27,0x94,0x30,0x2c,0x27,0x8a,0x33,0x8a,0x21,0x24,0x2a,0x8a,0x30,0x8a,0x33,0x2a,0x27,0x8a,0x30,0x8a,0x2e,0x2b,0x27,0x8a,0x33,0x8a,0x37,0x2b,0x22,0x8a,0x3a,0x8a,0x2b,0x27,0x22,0x8a,0x37,0x8a,0x33,0x2b,0x27,0x8a,0x2e,0x8a,0x30,0x2d,0x1d,0x94,0x33,0x2d,0x24,0x94,0x37,0x2c,0x26,0x8a,0x35,0x2c,0x8a,0x26,0x22,0x8a,0x33,0x2b,0x8a,0x27,0x1b,0x94,0x22,0x16,0x94,0x1f,0x13,0x8a,0x3f,0x37,0x8a,0x38,0x1d,0x11,0x8a,0x39,0x8a,0x3a,0x37,0x2e,0x94,0x3c,0x30,0x37,0x8a,0x3a,0x37,0x2e,0x8a,0x16,0x8a,0x37,0x33,0x2b,0x8a,0x38,0x2c,0x35,0x8a,0x39,0x36,0x2d,0x8a,0x3a,0x37,0x2e,0x94,0x3c,0x37,0x30,0x8a,0x2e,0x37,0x3a,0x8a,0x16,0x8a,0x37,0x8a,0x33,0x2b,0x27,0x8a,0x2e,0x8a,0x30,0x14,0x8a,0x32,0x8a,0x33,0x2c,0x27,0x8a,0x35,0x8a,0x37,0x20,0x8a,0x35,0x8a,0x33,0x23,0x27,0x8a,0x35,0x8a,0x2e,0x1f,0x8a,0x37,0x8a,0x38,0x27,0x2b,0x8a,0x3a,0x8a,0x3c,0x16,0x8a,0x3a,0x8a,0x37,0x2b,0x27,0x8a,0x38,0x8a,0x3a,0x37,0x2e,0x94,0x3c,0x30,0x37,0x8a,0x3a,0x37,0x2e,0x8a,0x16,0x8a,0x37,0x33,0x2b,0x8a,0x38,0x2c,0x35,0x8a,0x39,0x36,0x2d,0x8a,0x3a,0x37,0x2e,0x94,0x3c,0x37,0x30,0x8a,0x2e,0x37,0x3a,0x8a,0x1f,0x8a,0x3a,0x8a,0x3c,0x1e,0x8a,0x3d,0x8a,0x3e,0x3a,0x35,0x8a,0x3e,0x35,0x3a,0x8a,0x29,0x26,0x22,0x8a,0x3e,0x33,0x39,0x8a,0x1d,0x8a,0x3c,0x8a,0x39,0x33,0x27,0x8a,0x35,0x8a,0x3a,0x32,0x29,0x94,0x20,0x14,0x94,0x1f,0x13,0x8a,0x37,0x2b,0x33,0x8a,0x38,0x35,0x2c,0x8a,0x39,0x36,0x2d,0x8a,0x2e,0x3a,0x37,0x94,0x3c,0x37,0x30,0x8a,0x3a,0x37,0x2e,0x8a,0x16,0x8a,0x33,0x2b,0x37,0x8a,0x38,0x35,0x2c,0x8a,0x39,0x36,0x2d,0x8a,0x3a,0x37,0x2e,0x94,0x30,0x3c,0x37,0x8a,0x37,0x2e,0x3a,0x8a,0x16,0x8a,0x37,0x8a,0x33,0x2b,0x27,0x8a,0x2e,0x8a,0x30,0x14,0x8a,0x32,0x8a,0x33,0x27,0x24,0x8a,0x35,0x8a,0x37,0x20,0x8a,0x35,0x8a,0x33,0x2c,0x27,0x8a,0x35,0x8a,0x33,0x1f,0x94,0x2b,0x27,0x22,0x94,0x1b,0x8a,0x2e,0x8a,0x2d,0x2b,0x27,0x8a,0x2e,0x8a,0x33,0x2c,0x27,0x94,0x30,0x2c,0x20,0x8a,0x33,0x8a,0x2a,0x27,0x24,0x8a,0x30,0x8a,0x33,0x2a,0x27,0x8a,0x30,0x8a,0x2e,0x2b,0x22,0x8a,0x33,0x8a,0x37,0x2b,0x27,0x8a,0x3a,0x8a,0x2b,0x27,0x22,0x8a,0x37,0x8a,0x33,0x2b,0x22,0x8a,0x2e,0x8a,0x30,0x2d,0x27,0x94,0x33,0x2d,0x24,0x94,0x37,0x2c,0x26,0x8a,0x35,0x2c,0x8a,0x26,0x22,0x8a,0x33,0x2b,0x8a,0x27,0x1b,0x94,0x22,0x16,0x94,0x1b,0x0f,0x94,0x29,0x8a,0x2a,0x8a,0x2b,0x1b,0x8a,0x33,0x8a,0x27,0x1f,0x22,0x8a,0x2b,0x8a,0x33,0x22,0x16,0x94,0x2b,0x27,0x25,0x8a,0x33,0x8a,0x20,0x14,0x94,0x27,0x24,0x94,0x1f,0x13,0x8a,0x3f,0x37,0x33,0x8a,0x38,0x35,0x27,0x8a,0x39,0x36,0x8a,0x3a,0x37,0x16,0x8a,0x3f,0x37,0x33,0x8a,0x38,0x35,0x27,0x8a,0x3a,0x37,0x8a,0x16,0x8a,0x3e,0x35,0x32,0x8a,0x38,0x35,0x22,0x94,0x3f,0x37,0x33,0x94,0x27,0x22,0x1f,0x94,0x22,0x1f,0x27,0x94,0x29,0x26,0x22,0x8a,0x2a,0x8a,0x2b,0x1b,0x8a,0x33,0x8a,0x27,0x22,0x1f,0x8a,0x2b,0x8a,0x33,0x22,0x16,0x94,0x2b,0x27,0x22,0x8a,0x33,0x8a,0x20,0x14,0x94,0x27,0x24,0x94,0x1f,0x13,0x94,0x33,0x3c,0x30,0x8a,0x3a,0x33,0x2e,0x8a,0x39,0x33,0x2d,0x8a,0x3c,0x30,0x8a,0x3f,0x33,0x27,0x8a,0x37,0x8a,0x1d,0x8a,0x35,0x8a,0x3f,0x33,0x27,0x8a,0x3c,0x30,0x8a,0x35,0x38,0x26,0x94,0x22,0x16,0x94,0x24,0x18,0x94,0x29,0x26,0x1a,0x8a,0x2a,0x8a,0x2b,0x1b,0x8a,0x33,0x8a,0x1f,0x22,0x27,0x8a,0x2b,0x8a,0x33,0x22,0x16,0x94,0x2b,0x27,0x25,0x8a,0x33,0x8a,0x20,0x14,0x94,0x24,0x27,0x94,0x1f,0x13,0x8a,0x3f,0x37,0x33,0x8a,0x38,0x35,0x27,0x8a,0x39,0x36,0x8a,0x37,0x3a,0x16,0x8a,0x3f,0x33,0x37,0x8a,0x35,0x38,0x27,0x8a,0x3a,0x37,0x8a,0x16,0x8a,0x3e,0x35,0x32,0x8a,0x38,0x35,0x26,0x94,0x3f,0x37,0x33,0x94,0x27,0x22,0x1f,0x94,0x22,0x2b,0x27,0x94,0x3f,0x33,0x8a,0x35,0x8a,0x37,0x27,0x1b,0x8a,0x3f,0x33,0x8a,0x35,0x2b,0x27,0x8a,0x37,0x8a,0x25,0x19,0x8a,0x33,0x3f,0x8a,0x35,0x27,0x22,0x8a,0x3f,0x33,0x8a,0x37,0x24,0x18,0x8a,0x3f,0x33,0x8a,0x35,0x2c,0x27,0x8a,0x37,0x8a,0x23,0x17,0x8a,0x3f,0x33,0x8a,0x35,0x23,0x27,0x8a,0x3f,0x33,0x8a,0x3a,0x37,0x16,0x8a,0x33,0x37,0x3f,0x8a,0x38,0x35,0x2b,0x8a,0x3a,0x37,0x8a,0x16,0x8a,0x32,0x35,0x3e,0x8a,0x38,0x35,0x26,0x94,0x3f,0x37,0x33,0x94,0x22,0x16,0x94,0x3f,0x37,0x33,0xa8,0x38,0x3c,0x14,0x8a,0x3b,0x8a,0x3c,0x38,0x24,0x94,0x1b,0x94,0x3f,0x3c,0x38,0x94,0x38,0x3d,0x19,0x94,0x31,0x2c,0x29,0x8a,0x30,0x8a,0x31,0x20,0x8a,0x33,0x8a,0x35,0x2c,0x25,0x94,0x38,0x35,0x11,0x8a,0x37,0x8a,0x38,0x35,0x2c,0x94,0x18,0x94,0x3c,0x38,0x35,0x94,0x35,0x3a,0x3d,0x94,0x2e,0x25,0x29,0x8a,0x2d,0x8a,0x2e,0x1d,0x8a,0x30,0x8a,0x31,0x29,0x25,0x8a,0x3a,0x8a,0x35,0x25,0x19,0x94,0x3a,0x25,0x29,0x8a,0x35,0x8a,0x22,0x16,0x8a,0x3a,0x8a,0x35,0x23,0x17,0x94,0x33,0x24,0x18,0x94,0x24,0x27,0x2c,0x94,0x38,0x1d,0x94,0x2c,0x29,0x24,0x94,0x37,0x1f,0x8a,0x3b,0x8a,0x3e,0x26,0x29,0x8a,0x8a,0x23,0x8a,0x8a,0x3e,0x2b,0x29,0x8a,0x3f,0x8a,0x3c,0x2b,0x27,0xa8,0x3d,0x22,0x27,0x94,0x1b,0x94,0x3c,0x38,0x14,0x8a,0x3b,0x8a,0x38,0x3c,0x2c,0x94,0x1b,0x94,0x3f,0x3c,0x38,0x94,0x3d,0x38,0x19,0x94,0x31,0x2c,0x25,0x8a,0x30,0x8a,0x31,0x20,0x8a,0x33,0x8a,0x35,0x2c,0x29,0x94,0x38,0x35,0x11,0x8a,0x37,0x8a,0x38,0x35,0x2c,0x94,0x18,0x94,0x35,0x3c,0x38,0x94,0x3d,0x3a,0x35,0x94,0x2e,0x29,0x25,0x8a,0x2d,0x8a,0x2e,0x1d,0x8a,0x30,0x8a,0x31,0x29,0x25,0x8a,0x3a,0x8a,0x35,0x25,0x19,0x94,0x3a,0x29,0x25,0x8a,0x35,0x8a,0x16,0x22,0x8a,0x3a,0x8a,0x35,0x23,0x17,0x94,0x33,0x24,0x18,0x8a,0x20,0x14,0x8a,0x1f,0x13,0x8a,0x11,0x1d,0x8a,0x38,0x32,0x2f,0x9e,0x38,0x8a,0x3c,0x33,0x30,0x8a,0x3f,0x33,0x8a,0x2c,0x27,0x24,0x8a,0x3a,0x31,0x8a,0x27,0x1b,0x8a,0x33,0x8a,0x35,0x31,0x1b,0x8a,0x37,0x8a,0x38,0x30,0x20,0x94,0x32,0x8a,0x33,0x8a,0x35,0x8a,0x37,0x8a,0x38,0x8a,0x3a,0x8a,0x3c,0x38,0x14,0x8a,0x3b,0x8a,0x38,0x3c,0x2c,0x94,0x1b,0x94,0x3f,0x3c,0x38,0x94,0x3d,0x38,0x19,0x94,0x31,0x2c,0x29,0x8a,0x30,0x8a,0x31,0x20,0x8a,0x33,0x8a,0x35,0x29,0x2c,0x94,0x38,0x35,0x11,0x8a,0x37,0x8a,0x38,0x35,0x2c,0x94,0x18,0x94,0x3c,0x35,0x38,0x94,0x35,0x3d,0x3a,0x94,0x2e,0x29,0x25,0x8a,0x2d,0x8a,0x2e,0x1d,0x8a,0x30,0x8a,0x31,0x29,0x25,0x8a,0x3a,0x8a,0x35,0x25,0x19,0x94,0x3a,0x29,0x25,0x8a,0x35,0x8a,0x22,0x16,0x8a,0x3a,0x8a,0x35,0x23,0x17,0x94,0x33,0x24,0x18,0x94,0x2c,0x27,0x24,0x94,0x38,0x1d,0x94,0x2c,0x29,0x24,0x94,0x37,0x1f,0x8a,0x3b,0x8a,0x3e,0x26,0x2b,0x8a,0x8a,0x23,0x8a,0x8a,0x3e,0x2b,0x29,0x8a,0x3f,0x8a,0x3c,0x2b,0x27,0xa8,0x3d,0x2b,0x27,0x94,0x1b,0x94,0x3c,0x38,0x14,0x8a,0x3b,0x8a,0x38,0x3c,0x24,0x94,0x1b,0x94,0x3f,0x3c,0x38,0x94,0x3d,0x38,0x19,0x94,0x31,0x2c,0x29,0x8a,0x30,0x8a,0x31,0x20,0x8a,0x33,0x8a,0x35,0x2c,0x29,0x94,0x35,0x38,0x11,0x8a,0x37,0x8a,0x38,0x35,0x2c,0x94,0x18,0x94,0x3c,0x38,0x35,0x94,0x3d,0x3a,0x35,0x94,0x2e,0x25,0x29,0x8a,0x2d,0x8a,0x2e,0x1d,0x8a,0x30,0x8a,0x31,0x29,0x25,0x8a,0x3a,0x8a,0x35,0x25,0x19,0x94,0x3a,0x29,0x25,0x8a,0x35,0x8a,0x22,0x16,0x8a,0x3a,0x8a,0x35,0x23,0x17,0x94,0x33,0x24,0x18,0x8a,0x20,0x14,0x8a,0x1f,0x13,0x8a,0x1d,0x11,0x8a,0x38,0x32,0x2f,0x9e,0x38,0x8a,0x30,0x33,0x3c,0x8a,0x3f,0x33,0x8a,0x2c,0x27,0x24,0x8a,0x3a,0x31,0x8a,0x27,0x1b,0x8a,0x33,0x8a,0x35,0x31,0x1b,0x8a,0x37,0x8a,0x30,0x38,0x20,0xa8,0x3f,0x3c,0x38,0xa8,0x33,0x20,0x27,0x94,0x30,0x2c,0x27,0x8a,0x33,0x8a,0x2a,0x27,0x24,0x8a,0x30,0x8a,0x33,0x2a,0x21,0x8a,0x30,0x8a,0x2e,0x2b,0x27,0x8a,0x33,0x8a,0x37,0x22,0x27,0x8a,0x3a,0x8a,0x2b,0x27,0x22,0x8a,0x37,0x8a,0x33,0x2b,0x27,0x8a,0x2e,0x8a,0x30,0x2d,0x27,0x94,0x33,0x2d,0x24,0x94,0x37,0x2c,0x26,0x8a,0x35,0x2c,0x8a,0x26,0x22,0x8a,0x33,0x2b,0x8a,0x27,0x1b,0xa8,0x33,0x37,0x3a,0xa8,0x2c,0x29,0x14,0x94,0x2b,0x28,0x20,0x8a,0x2c,0x29,0x8a,0x18,0x8a,0x2b,0x28,0x8a,0x29,0x2c,0x24,0x94,0x14,0x8a,0x30,0x8a,0x2c,0x35,0x24,0x8a,0x30,0x8a,0x33,0x18,0x8a,0x35,0x8a,0x33,0x24,0x20,0x8a,0x30,0x8a,0x2e,0x2b,0x1b,0x94,0x2d,0x2a,0x27,0x8a,0x2e,0x2b,0x8a,0x16,0x8a,0x2d,0x2a,0x8a,0x2e,0x2b,0x1f,0x94,0x1b,0x8a,0x33,0x8a,0x37,0x2e,0x27,0x8a,0x33,0x8a,0x35,0x16,0x8a,0x37,0x8a,0x35,0x1f,0x22,0x8a,0x33,0x8a,0x35,0x32,0x16,0x94,0x34,0x31,0x26,0x8a,0x35,0x32,0x8a,0x1a,0x8a,0x34,0x31,0x8a,0x32,0x35,0x26,0x94,0x16,0x8a,0x38,0x8a,0x3c,0x32,0x22,0x8a,0x38,0x8a,0x3a,0x1d,0x8a,0x3c,0x8a,0x3a,0x26,0x22,0x8a,0x38,0x8a,0x3f,0x33,0x27,0x8a,0x3f,0x33,0x8a,0x3f,0x33,0x27,0xa8,0x3c,0x33,0x27,0x94,0x3a,0x33,0x1f,0x94,0x2e,0x2b,0x8a,0x2e,0x2b,0x8a,0x2e,0x2b,0x94,0x2b,0x2e,0x94,0x2c,0x29,0x14,0x94,0x2b,0x28,0x24,0x8a,0x2c,0x29,0x8a,0x18,0x8a,0x2b,0x28,0x8a,0x2c,0x29,0x24,0x94,0x14,0x8a,0x30,0x8a,0x35,0x2c,0x20,0x8a,0x30,0x8a,0x33,0x18,0x8a,0x35,0x8a,0x33,0x24,0x20,0x8a,0x30,0x8a,0x2e,0x2b,0x1b,0x94,0x2a,0x2d,0x27,0x8a,0x2e,0x2b,0x8a,0x16,0x8a,0x2d,0x2a,0x8a,0x2e,0x2b,0x1f,0x94,0x1b,0x8a,0x33,0x8a,0x37,0x2e,0x27,0x8a,0x33,0x8a,0x35,0x16,0x8a,0x37,0x8a,0x35,0x27,0x22,0x8a,0x33,0x8a,0x30,0x20,0x14,0x8a,0x2f,0x8a,0x30,0x1d,0x11,0x8a,0x3a,0x30,0x8a,0x1f,0x13,0x8a,0x38,0x30,0x8a,0x20,0x14,0x8a,0x33,0x30,0x8a,0x37,0x2e,0x22,0x8a,0x36,0x8a,0x37,0x22,0x2b,0x8a,0x3c,0x8a,0x2a,0x27,0x21,0x8a,0x3f,0x8a,0x3a,0x2b,0x27,0x8a,0x37,0x8a,0x33,0x2d,0x18,0x94,0x33,0x2d,0x1d,0x94,0x37,0x2c,0x32,0x8a,0x35,0x32,0x2c,0x8a,0x26,0x1a,0x8a,0x33,0x2e,0x2b,0x8a,0x27,0x1b,0x94,0x2b,0x2e,0x8a,0x2e,0x2b,0x8a,0x2e,0x2b,0x94,0x2e,0x2b,0x94,0x2c,0x29,0x14,0x94,0x2b,0x28,0x20,0x8a,0x29,0x2c,0x8a,0x18,0x8a,0x2b,0x28,0x8a,0x2c,0x29,0x24,0x94,0x14,0x8a,0x30,0x8a,0x35,0x2c,0x24,0x8a,0x30,0x8a,0x33,0x18,0x8a,0x35,0x8a,0x33,0x24,0x20,0x8a,0x30,0x8a,0x2e,0x2b,0x1b,0x94,0x2d,0x2a,0x27,0x8a,0x2e,0x2b,0x8a,0x16,0x8a,0x2d,0x2a,0x8a,0x2e,0x2b,0x1f,0x94,0x1b,0x8a,0x33,0x8a,0x37,0x2e,0x27,0x8a,0x33,0x8a,0x35,0x16,0x8a,0x37,0x8a,0x35,0x27,0x22,0x8a,0x33,0x8a,0x35,0x32,0x16,0x94,0x31,0x34,0x26,0x8a,0x35,0x32,0x8a,0x1a,0x8a,0x34,0x31,0x8a,0x35,0x32,0x26,0x94,0x16,0x8a,0x38,0x8a,0x3c,0x32,0x20,0x8a,0x38,0x8a,0x3a,0x1d,0x8a,0x3c,0x8a,0x3a,0x26,0x22,0x8a,0x38,0x8a,0x33,0x3f,0x27,0x8a,0x3f,0x33,0x8a,0x3f,0x33,0x1e,0xa8,0x3c,0x33,0x27,0x94,0x3a,0x33,0x27,0x94,0x2b,0x2e,0x8a,0x2e,0x2b,0x8a,0x2e,0x2b,0x94,0x2e,0x2b,0x94,0x2c,0x29,0x14,0x94,0x2b,0x28,0x20,0x8a,0x2c,0x29,0x8a,0x18,0x8a,0x2b,0x28,0x8a,0x2c,0x29,0x24,0x94,0x14,0x8a,0x30,0x8a,0x35,0x2c,0x24,0x8a,0x30,0x8a,0x33,0x18,0x8a,0x35,0x8a,0x33,0x24,0x20,0x8a,0x30,0x8a,0x2e,0x2b,0x1b,0x94,0x2d,0x2a,0x27,0x8a,0x2e,0x2b,0x8a,0x16,0x8a,0x2a,0x2d,0x8a,0x2e,0x2b,0x27,0x94,0x1b,0x8a,0x33,0x8a,0x37,0x2e,0x27,0x8a,0x33,0x8a,0x35,0x16,0x8a,0x37,0x8a,0x35,0x22,0x1f,0x8a,0x33,0x8a,0x30,0x20,0x14,0x8a,0x2f,0x8a,0x30,0x1d,0x11,0x8a,0x3a,0x30,0x8a,0x1f,0x13,0x8a,0x38,0x30,0x8a,0x20,0x14,0x8a,0x33,0x30,0x8a,0x37,0x2e,0x22,0x8a,0x36,0x8a,0x37,0x2b,0x27,0x8a,0x3c,0x8a,0x21,0x27,0x2a,0x8a,0x3f,0x8a,0x3a,0x2b,0x27,0x8a,0x37,0x8a,0x33,0x2d,0x24,0x94,0x33,0x2d,0x1d,0x94,0x37,0x32,0x2c,0x8a,0x35,0x2c,0x32,0x8a,0x26,0x1a,0x8a,0x33,0x2e,0x2b,0x8a,0x27,0x1b,0x94,0x22,0x16,0x94,0x3f,0x3a,0x37,0xff
};

extern void music_tick(void);
extern void music_duty(void);
extern void music_start(const char*);
extern char* music_get_ptr(void);
extern char music_is_done(void);

unsigned char music_update() {
  music_tick();
  music_duty();
  return IRQ_NOT_HANDLED;
}

void main() {
  // clear the screen
  clrscr();
  // position the cursor, output text
  gotoxy(0,1);
  // print some text
  cputs("Hello Atari 8-bit World!\r\n");

  // start music and set interrupt
  music_start(music1);
  set_irq(music_update, (void*)0x9f00, 0x100);

  // cartridge ROMs do not exit, so loop forever
  while (1) {
    gotoxy(1,2);
    printf("%04x %02x %d",
           music_get_ptr(), *music_get_ptr(), music_is_done());
  }
}