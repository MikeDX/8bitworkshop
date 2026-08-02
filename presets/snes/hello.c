/* SNES C hello — "HELLO WORLD" via BG1 tiles.
 *
 * Local:  ./scripts/build_snes_local.sh hello.c mame
 */
//#resource "hello_tiles.chr"

void poke8(unsigned addr, unsigned char val);
void dma_to_ppu(unsigned src, unsigned char dest_reg, unsigned size, unsigned char mode);

extern const unsigned char hello_tiles[];

#define REG_INIDISP  0x2100
#define REG_BGMODE   0x2105
#define REG_BG1SC    0x2107
#define REG_BG12NBA  0x210B
#define REG_VMAIN    0x2115
#define REG_VMADDL   0x2116
#define REG_VMDATA   0x2118
#define REG_CGADD    0x2121
#define REG_CGDATA   0x2122
#define REG_TM       0x212C

/* SNES BGR555 color as two CGDATA writes (low byte first). */
static void cgram_color(unsigned short rgb) {
    poke8(REG_CGDATA, (unsigned char)(rgb & 0xFF));
    poke8(REG_CGDATA, (unsigned char)(rgb >> 8));
}

static void vram_addr(unsigned addr) {
    poke8(REG_VMADDL, (unsigned char)(addr & 0xFF));
    poke8(REG_VMADDL + 1, (unsigned char)(addr >> 8));
}

void main(void) {
    /* Force blank during setup. */
    poke8(REG_INIDISP, 0x8F);

    /* Load 4bpp tiles into VRAM $0000 (DMA mode 1 = VMDATAL/H). */
    poke8(REG_VMAIN, 0x80);
    vram_addr(0x0000);
    dma_to_ppu((unsigned)hello_tiles, 0x18, 8192, 0x01);

    /* Palette: pink backdrop + white text (index 15), matching hello.wiz. */
    poke8(REG_CGADD, 0);
    cgram_color((0x1C) | (0x10 << 5) | (0x12 << 10)); /* 0 */
    cgram_color((0x06) | (0x06 << 5) | (0x0C << 10)); /* 1 */
    cgram_color(0);
    cgram_color(0);
    cgram_color(0);
    cgram_color(0);
    cgram_color(0);
    cgram_color(0);
    cgram_color(0);
    cgram_color(0);
    cgram_color(0);
    cgram_color(0);
    cgram_color(0);
    cgram_color(0);
    cgram_color(0);
    cgram_color(0x7FFF); /* 15 white */

    /* Tilemap at VRAM $1000: clear then write message (tile # = ASCII). */
    poke8(REG_VMAIN, 0x00); /* increment after low write */
    vram_addr(0x1000);
    {
        unsigned i;
        for (i = 0; i < 32 * 28; i++)
            poke8(REG_VMDATA, 0);
    }
    /* (10, 13) on 32-wide map */
    vram_addr(0x1000 + 13 * 32 + 10);
    poke8(REG_VMDATA, 'H');
    poke8(REG_VMDATA, 'E');
    poke8(REG_VMDATA, 'L');
    poke8(REG_VMDATA, 'L');
    poke8(REG_VMDATA, 'O');
    poke8(REG_VMDATA, ' ');
    poke8(REG_VMDATA, 'W');
    poke8(REG_VMDATA, 'O');
    poke8(REG_VMDATA, 'R');
    poke8(REG_VMDATA, 'L');
    poke8(REG_VMDATA, 'D');

    /* Mode 1 BG1, tilemap $1000, tiles $0000. */
    poke8(REG_BGMODE, 0x01);
    poke8(REG_BG1SC, (0x1000 >> 8) & 0xFC);
    poke8(REG_BG12NBA, 0x00);
    poke8(REG_TM, 0x01);
    poke8(REG_INIDISP, 0x0F);

    while (1) {
    }
}
