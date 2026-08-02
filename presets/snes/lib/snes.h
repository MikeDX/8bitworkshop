/* Thin SNES helpers for 8bitworkshop (tcc816 + WLA).
 * Enough for Mode 7 and simple 2D setup — PVSnesLib-shaped, not a full port.
 */
#ifndef SNES_H
#define SNES_H

/* Registers (write-only PPU unless noted) */
#define REG_INIDISP  0x2100
#define REG_OBSEL    0x2101
#define REG_BGMODE   0x2105
#define REG_MOSAIC   0x2106
#define REG_BG1SC    0x2107
#define REG_BG12NBA  0x210B
#define REG_BG1HOFS  0x210D /* write twice; Mode7 H scroll */
#define REG_BG1VOFS  0x210E /* write twice; Mode7 V scroll */
#define REG_VMAIN    0x2115
#define REG_VMADDL   0x2116
#define REG_VMDATA   0x2118
#define REG_VMDATAH  0x2119
#define REG_M7SEL    0x211A
#define REG_M7A      0x211B /* write twice, 8.8 fixed */
#define REG_M7B      0x211C
#define REG_M7C      0x211D
#define REG_M7D      0x211E
#define REG_M7X      0x211F /* write twice */
#define REG_M7Y      0x2120
#define REG_CGADD    0x2121
#define REG_CGDATA   0x2122
#define REG_TM       0x212C
#define REG_TS       0x212D
#define REG_NMITIMEN 0x4200
#define REG_RDNMI    0x4210 /* read */

#define M7_WRAP      (1 << 6)
#define M7_OUTTRANS  (2 << 6)
#define M7_OUTTILE   (3 << 6)

#define RGB15(r, g, b) ((unsigned)((r) & 31) | (((g) & 31) << 5) | (((b) & 31) << 10))

void poke8(unsigned addr, unsigned char val);
/* Write low then high to a write-twice register (M7*, BG*OFS). */
void poke16(unsigned addr, unsigned val);

/* DMA channel 0 → PPU. `src` may be a far pointer (ROM/WRAM symbol).
 * dest_reg: 0x18=VMDATAL, 0x19=VMDATAH, 0x22=CGDATA
 * mode: usually 0x00 (1 reg) or 0x01 (2 regs low/high)
 */
void dma_to_ppu(unsigned src, unsigned char dest_reg, unsigned size, unsigned char mode);

void snes_wait_vblank(void);
void snes_brightness(unsigned char level); /* 0..15, or |0x80 force blank */

void snes_cgram(unsigned char index, unsigned color);
void snes_vram_addr(unsigned word_addr);

/* Mode 7: BGMODE=7, identity matrix, BG1 on, center/scroll defaults. */
void snes_mode7_begin(unsigned char m7sel);
void snes_mode7_matrix(unsigned a, unsigned b, unsigned c, unsigned d);
void snes_mode7_center(unsigned x, unsigned y);
void snes_mode7_scroll(unsigned x, unsigned y);
/* angle 0..255, scale 8.8 (0x0100 = 1.0) */
void snes_mode7_rot(unsigned char angle, unsigned scale);

/* Upload Mode7 map (low bytes) / tiles (high bytes). size in bytes. */
void snes_mode7_load_map(unsigned src, unsigned word_addr, unsigned size);
void snes_mode7_load_tiles(unsigned src, unsigned word_addr, unsigned size);

#endif
