import { MOS6502 } from "../common/cpu/MOS6502";
import { BasicScanlineMachine } from "../common/devices";
import { Keys, makeKeycodeMap, newAddressDecoder, newKeyboardHandler } from "../common/emu";

// Data East Pocket Gal (pcktgal / pcktgal2) — based on MAME pcktgal.cpp + decbac06
 // https://github.com/mamedev/mame/blob/master/src/mame/dataeast/pcktgal.cpp

const PCKTGAL_KEYCODE_MAP = makeKeycodeMap([
    [Keys.RIGHT, 0, -0x01],
    [Keys.LEFT, 0, -0x02],
    [Keys.DOWN, 0, -0x04],
    [Keys.UP, 0, -0x08],
    [Keys.START, 0, -0x10],
    [Keys.VK_2, 0, -0x20],
    [Keys.B, 0, -0x40],
    [Keys.A, 0, -0x80],
    [Keys.P2_RIGHT, 1, -0x01],
    [Keys.P2_LEFT, 1, -0x02],
    [Keys.P2_DOWN, 1, -0x04],
    [Keys.P2_UP, 1, -0x08],
    [Keys.SELECT, 1, -0x10], // coin 1
    [Keys.OPTION, 1, -0x20],  // coin 2
    [Keys.P2_B, 1, -0x40],
    [Keys.P2_A, 1, -0x80],
]);

/*
Combined homebrew ROM (ld65 output):
  0x00000..0x07FFF  program ROM mapped at $8000-$FFFF (32 KB)
  0x08000..0x0FFFF  char ROM (32 KB = 1024 tiles × 32 bytes, 4bpp planar)
  0x10000..0x13FFF  sprite ROM (16 KB = 256 sprites × 64 bytes, 2bpp planar)
  0x14000..0x143FF  palette PROMs (512 + 512 bytes)
*/

export const PCKTGAL_PRG_SIZE = 0x8000;
export const PCKTGAL_CHARS_SIZE = 0x8000;
export const PCKTGAL_SPRITES_SIZE = 0x4000;
export const PCKTGAL_PROMS_SIZE = 0x400;
export const PCKTGAL_ROM_SIZE =
    PCKTGAL_PRG_SIZE + PCKTGAL_CHARS_SIZE + PCKTGAL_SPRITES_SIZE + PCKTGAL_PROMS_SIZE;

const CHARS_OFS = PCKTGAL_PRG_SIZE;
const SPRITES_OFS = CHARS_OFS + PCKTGAL_CHARS_SIZE;
const PROMS_OFS = SPRITES_OFS + PCKTGAL_SPRITES_SIZE;

function promNibble(n: number): number {
    return 0x0e * ((n >> 0) & 1) + 0x1f * ((n >> 1) & 1) + 0x43 * ((n >> 2) & 1) + 0x8f * ((n >> 3) & 1);
}

export class PocketGalMachine extends BasicScanlineMachine {
    cpuFrequency = 2000000;
    sampleRate = 44100;
    numVisibleScanlines = 224;
    numTotalScanlines = 262;
    cpuCyclesPerLine = Math.floor(2000000 / 60 / 262);
    canvasWidth = 256;
    defaultROMSize = PCKTGAL_ROM_SIZE;
    cpu = new MOS6502();

    ram = new Uint8Array(0x800);
    // BAC06 internal VRAM is 0x2000 words; CPU only maps the first 0x400
    // (0x0800-0x0FFF). Extra words stay 0 → blank when scrolling past 256px.
    vram = new Uint8Array(0x4000);
    spriteram = new Uint8Array(0x200);
    pf_control0 = new Uint8Array(8);    // byte view of ctrlreg writes
    pf_control1 = new Uint16Array(8);   // scrollreg words (big-endian)
    mainbank = [0, 0];
    soundlatch = 0;

    maincpu = new Uint8Array(0x10000);
    charRom = new Uint8Array(PCKTGAL_CHARS_SIZE);
    spriteRom = new Uint8Array(PCKTGAL_SPRITES_SIZE);
    proms = new Uint8Array(PCKTGAL_PROMS_SIZE);
    palette = new Uint32Array(512);

    inputs = new Uint8Array([0xff, 0xff, 0xff]); // P1, P2, DSW — active low
    keyMap = PCKTGAL_KEYCODE_MAP;
    handler = newKeyboardHandler(this.inputs, this.keyMap);

    bus = {
        read: newAddressDecoder([
            [0x0000, 0x07ff, 0, (a) => this.ram[a]],
            [0x0800, 0x0fff, 0, (a) => this.vram[a - 0x0800]],
            [0x1000, 0x11ff, 0, (a) => this.spriteram[a - 0x1000]],
            [0x1800, 0x1800, 0, () => this.inputs[0]],
            [0x1810, 0x181f, 0, (a) => this.readScrollReg(a - 0x1810)],
            [0x1a00, 0x1a00, 0, () => this.inputs[1]],
            [0x1c00, 0x1c00, 0, () => this.inputs[2]],
            [0x4000, 0x5fff, 0, (a) => this.readBank(0, a - 0x4000)],
            [0x6000, 0x7fff, 0, (a) => this.readBank(1, a - 0x6000)],
            [0x8000, 0xffff, 0, (a) => this.maincpu[a]],
        ]),
        write: newAddressDecoder([
            [0x0000, 0x07ff, 0, (a, v) => { this.ram[a] = v; }],
            [0x0800, 0x0fff, 0, (a, v) => { this.vram[a - 0x0800] = v; }],
            [0x1000, 0x11ff, 0, (a, v) => { this.spriteram[a - 0x1000] = v; }],
            // MAME ctrlreg8_w: only low byte of word[offset>>1]
            [0x1800, 0x1807, 0, (a, v) => { this.pf_control0[(a - 0x1800) >> 1] = v; }],
            [0x1810, 0x181f, 0, (a, v) => { this.writeScrollReg(a - 0x1810, v); }],
            [0x1a00, 0x1a00, 0, (_a, v) => { this.soundlatch = v; }],
            [0x1c00, 0x1c00, 0, (_a, v) => {
                this.mainbank[0] = v & 1;
                this.mainbank[1] = (v >> 1) & 1;
            }],
        ]),
    };

    constructor() {
        super();
        this.connectCPUMemoryBus(this);
        this.pf_control0[0] = 0x03; // 8x8 tile mode, no TILE_FLIPX
        this.inputs[2] = 0xff; // DSW defaults
        this.buildDefaultPalette();
    }

    readBank(which: number, offset: number): number {
        // bank0: ROM $0000 or $4000; bank1: ROM $2000 or $6000
        const bases0 = [0x0000, 0x4000];
        const bases1 = [0x2000, 0x6000];
        const base = which === 0 ? bases0[this.mainbank[0]] : bases1[this.mainbank[1]];
        return this.maincpu[base + offset];
    }

    /** Big-endian scroll registers (MAME scrollreg8, !IsLittleEndian). */
    readScrollReg(offset: number): number {
        if (offset < 4) {
            const word = this.pf_control1[offset >> 1];
            return (offset & 1) ? (word & 0xff) : (word >> 8);
        }
        return this.pf_control1[offset >> 1] & 0xff;
    }

    writeScrollReg(offset: number, data: number): void {
        if (offset < 4) {
            const idx = offset >> 1;
            if (offset & 1) {
                this.pf_control1[idx] = (this.pf_control1[idx] & 0xff00) | data;
            } else {
                this.pf_control1[idx] = (this.pf_control1[idx] & 0x00ff) | (data << 8);
            }
        } else {
            this.pf_control1[offset >> 1] = data;
        }
    }

    /**
     * BAC06 8×8 tilemap mode from ctrlreg[3] (default 0).
     * MAME: mode0=128×32 (1024×256), mode1=64×64 (512×512), mode2=32×128 (256×1024).
     */
    getPfMode(): number {
        let mode = this.pf_control0[3] & 3;
        if (mode === 3) mode = 1;
        return mode;
    }

    /** VRAM word index for 8×8 BAC06 scan (matches MAME tile_shape*_8x8_scan). */
    tileIndex8x8(col: number, row: number, mode: number): number {
        switch (mode) {
            case 1: // 64×64
                return (col & 0x1f) + ((row & 0x1f) << 5) + ((row & 0x20) << 5) + ((col & 0x20) << 6);
            case 2: // 32×128
                return (col & 0x1f) + ((row & 0x7f) << 5);
            default: // 128×32
                return (col & 0x1f) + ((row & 0x1f) << 5) + ((col & 0x60) << 5);
        }
    }

    loadROM(rom: Uint8Array) {
        super.loadROM(rom);
        this.maincpu.fill(0);
        // Program always fills $8000-$FFFF
        const prgLen = Math.min(rom.length, PCKTGAL_PRG_SIZE);
        this.maincpu.set(rom.subarray(0, prgLen), 0x8000);

        if (rom.length > CHARS_OFS) {
            this.charRom.fill(0);
            this.charRom.set(rom.subarray(CHARS_OFS, Math.min(rom.length, SPRITES_OFS)));
        }
        if (rom.length > SPRITES_OFS) {
            this.spriteRom.fill(0);
            this.spriteRom.set(rom.subarray(SPRITES_OFS, Math.min(rom.length, PROMS_OFS)));
        }
        if (rom.length > PROMS_OFS) {
            this.proms.fill(0);
            this.proms.set(rom.subarray(PROMS_OFS, Math.min(rom.length, PROMS_OFS + PCKTGAL_PROMS_SIZE)));
            this.buildPaletteFromProms();
        } else {
            this.buildDefaultPalette();
        }
    }

    buildDefaultPalette() {
        for (let i = 0; i < 512; i++) {
            const r = (i & 15) * 17;
            const g = ((i >> 4) & 15) * 17;
            const b = ((i >> 8) & 15) * 17;
            this.palette[i] = 0xff000000 | (b << 16) | (g << 8) | r;
        }
        // Useful opaque colors for demos
        this.palette[256] = 0xff000000;
        this.palette[257] = 0xffffffff;
        this.palette[258] = 0xff00ffff;
        this.palette[259] = 0xffff00ff;
        this.palette[260] = 0xffffff00;
        this.palette[0] = 0xff000000;
        this.palette[1] = 0xff4040ff;
        this.palette[2] = 0xff40ff40;
        this.palette[3] = 0xffffffff;
    }

    buildPaletteFromProms() {
        for (let i = 0; i < 512; i++) {
            const rg = this.proms[i];
            const bb = this.proms[i + 512] ?? 0;
            const r = promNibble(rg & 0x0f);
            const g = promNibble(rg >> 4);
            const b = promNibble(bb & 0x0f);
            this.palette[i] = 0xff000000 | (b << 16) | (g << 8) | r;
        }
    }

    read(a: number): number { return this.bus.read(a); }
    readConst(a: number): number { return this.bus.read(a); }
    write(a: number, v: number): void { this.bus.write(a, v); }

    /** Big-endian VRAM word (BAC06 internal; out-of-range → 0). */
    getTileWord(index: number): number {
        const ofs = index * 2;
        if (ofs < 0 || ofs + 1 >= this.vram.length) return 0;
        return (this.vram[ofs] << 8) | this.vram[ofs + 1];
    }

    getCharPixel(tile: number, x: number, y: number): number {
        const base = (tile & 0x3ff) * 32;
        let c = 0;
        for (let p = 0; p < 4; p++) {
            const byte = this.charRom[base + p * 8 + (y & 7)];
            if (byte & (0x80 >> (x & 7))) c |= 1 << p;
        }
        return c;
    }

    getSpritePixel(code: number, x: number, y: number): number {
        // Homebrew: 64 bytes/sprite — plane0 at code*64, plane1 at code*64+32
        // Within plane: row-major left-then-right (2 bytes/row) for Asset Editor friendliness
        const base = (code & 0xff) * 64;
        const lx = x & 15;
        const ly = y & 15;
        const byteOfs = ly * 2 + (lx >> 3);
        const bit = 0x80 >> (lx & 7);
        let c = 0;
        if (this.spriteRom[base + byteOfs] & bit) c |= 1;
        if (this.spriteRom[base + 32 + byteOfs] & bit) c |= 2;
        return c;
    }

    startScanline(): void { }

    drawScanline(): void {
        const screenY = this.scanline; // 0..223 visible
        if (screenY >= this.numVisibleScanlines) return;

        const ctrl0 = this.pf_control0[0];
        const flip = !!(ctrl0 & 0x80);
        const scrollx = this.pf_control1[0] & 0xffff;
        const scrolly = this.pf_control1[1] & 0xffff;
        const mode = this.getPfMode();
        // Pixel sizes match MAME m_tilemap_8x8[*] create() sizes
        const widthMask = mode === 1 ? 0x1ff : mode === 2 ? 0xff : 0x3ff;
        const heightMask = mode === 1 ? 0x1ff : mode === 2 ? 0x3ff : 0xff;

        // Visible area matches MAME rows 16..239 → map to internal Y
        const internalY = screenY + 16;
        let srcY = flip ? (heightMask + 1 - 256) - scrolly + internalY : scrolly + internalY;
        srcY &= heightMask;

        const tileFlipX = !(ctrl0 & 2); // MAME: TILE_FLIPX when bit1 clear
        const yofs = screenY * this.canvasWidth;
        for (let screenX = 0; screenX < 256; screenX++) {
            let srcX = flip ? (widthMask + 1 - 256) - scrollx + screenX : scrollx + screenX;
            srcX &= widthMask;

            const col = srcX >> 3;
            const row = srcY >> 3;
            const word = this.getTileWord(this.tileIndex8x8(col, row, mode));
            const tile = word & 0xfff;
            const colour = (word >> 12) & 0xf;
            let px = srcX & 7;
            const py = srcY & 7;
            if (tileFlipX) px = 7 - px;
            const pix = this.getCharPixel(tile, px, py);
            const pen = 256 + colour * 16 + pix;
            this.pixels[yofs + screenX] = this.palette[pen];
        }

        this.drawSpritesOnLine(screenY, flip);
    }

    drawSpritesOnLine(screenY: number, flipScreen: boolean): void {
        const yofs = screenY * this.canvasWidth;
        const internalY = screenY + 16;

        for (let offs = 0; offs < 0x200; offs += 4) {
            const syRaw = this.spriteram[offs];
            if (syRaw === 0xf8) continue;

            let sx = 240 - this.spriteram[offs + 2];
            let sy = 240 - syRaw;
            let flipx = !!(this.spriteram[offs + 1] & 0x04);
            let flipy = !!(this.spriteram[offs + 1] & 0x02);
            if (flipScreen) {
                sx = 240 - sx;
                sy = 240 - sy;
                flipx = !flipx;
                flipy = !flipy;
            }

            const code = this.spriteram[offs + 3] + ((this.spriteram[offs + 1] & 1) << 8);
            const colour = (this.spriteram[offs + 1] & 0x70) >> 4;

            const row = internalY - sy;
            if (row < 0 || row >= 16) continue;
            const py = flipy ? (15 - row) : row;

            for (let col = 0; col < 16; col++) {
                const px = flipx ? (15 - col) : col;
                const pix = this.getSpritePixel(code, px, py);
                if (pix === 0) continue;
                const dx = sx + col;
                if (dx < 0 || dx >= 256) continue;
                const pen = colour * 4 + pix;
                this.pixels[yofs + dx] = this.palette[pen];
            }
        }
    }

    postFrame() {
        this.cpu.NMI();
    }

    getVideoParams() {
        return { width: 256, height: 224, aspect: 4 / 3 };
    }
}
