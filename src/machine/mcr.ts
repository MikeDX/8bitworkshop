// based on https://raw.githubusercontent.com/mamedev/mame/refs/heads/master/src/mame/midway/mcr.cpp
// license:BSD-3-Clause
// copyright-holders:Aaron Giles

import { Z80, Z80State } from "../common/cpu/ZilogZ80";
import { BasicScanlineMachine } from "../common/devices";
import { KeyFlags, newAddressDecoder, padBytes, Keys, makeKeycodeMap, newKeyboardHandler, EmuHalt } from "../common/emu";
import { MasterAudio, AY38910_Audio, TssChannelAdapter } from "../common/audio";

// MCR-II constants (91490 CPU board)
const MCR2_XTAL = 19968000;
const MCR2_CPU_FREQ = MCR2_XTAL / 4; // ~4.992 MHz
const MCR2_NUM_VISIBLE_SCANLINES = 480;
const MCR2_NUM_TOTAL_SCANLINES = 525;
const MCR2_CANVAS_WIDTH = 512;
const MCR2_FPS = 30;
const MCR2_CYCLES_PER_LINE = Math.floor(MCR2_CPU_FREQ / (MCR2_NUM_TOTAL_SCANLINES * MCR2_FPS));

const MCR2_TILE_COLS = 32;
const MCR2_TILE_ROWS = 30;
const MCR2_TILE_SIZE = 8; // double-pixels for BG

const INITIAL_WATCHDOG = 16;

function pal3bit(v: number): number {
    v &= 7;
    return (v << 5) | (v << 2) | (v >> 1);
}

const MCR2_KEYCODE_MAP = makeKeycodeMap([
    [Keys.SELECT, 0, 0x1],    // Coin 1
    [Keys.START, 0, 0x4],     // 1P Start
    [Keys.VK_2, 0, 0x8],      // 2P Start
    [Keys.UP, 1, 0x1],        // P1 Up
    [Keys.DOWN, 1, 0x2],      // P1 Down
    [Keys.LEFT, 1, 0x4],      // P1 Left
    [Keys.RIGHT, 1, 0x8],     // P1 Right
    [Keys.A, 1, 0x10],        // P1 Button 1
    [Keys.B, 1, 0x20],        // P1 Button 2
    [Keys.P2_UP, 2, 0x1],     // P2 Up
    [Keys.P2_DOWN, 2, 0x2],   // P2 Down
    [Keys.P2_LEFT, 2, 0x4],   // P2 Left
    [Keys.P2_RIGHT, 2, 0x8],  // P2 Right
    [Keys.P2_A, 2, 0x10],     // P2 Button 1
    [Keys.P2_B, 2, 0x20],     // P2 Button 2
]);

/*
 * Homebrew ROM blob layout (fits SDCC IHX; keeps 0xE000+ free for RAM):
 *   0x0000-0x7FFF  program
 *   0x8000-0x9FFF  background tiles (2 planes × 4KB) — tile*8, up to 512 codes
 *   0xA000-0xBFFF  sprites (4 planes × 2KB) — 32×32, row-major, 16 codes
 *   0xC000-0xFFFF  unused in blob / CPU RAM from 0xE000
 */
const ROM_BG_GFX_START = 0x8000;
const ROM_BG_GFX_SIZE = 0x2000;
const ROM_SPR_GFX_START = 0xA000;
const ROM_SPR_GFX_SIZE = 0x2000;
const ROM_TOTAL_SIZE = 0xC000;

export class MCR2Machine extends BasicScanlineMachine {

    cpuFrequency = MCR2_CPU_FREQ;
    canvasWidth = MCR2_CANVAS_WIDTH;
    numVisibleScanlines = MCR2_NUM_VISIBLE_SCANLINES;
    numTotalScanlines = MCR2_NUM_TOTAL_SCANLINES;
    cpuCyclesPerLine = MCR2_CYCLES_PER_LINE;
    defaultROMSize = ROM_TOTAL_SIZE;
    sampleRate = MCR2_FPS * MCR2_NUM_TOTAL_SCANLINES * 2;

    cpu = new Z80();
    ram = new Uint8Array(0x800);      // E000-E7FF (NVRAM)
    sprram = new Uint8Array(0x200);   // E800-E9FF (sprite RAM)
    vram = new Uint8Array(0x800);     // F000-F7FF (video RAM)
    palram = new Uint8Array(0x80);    // F800-F87F (palette RAM)

    palette = new Uint32Array(64);

    interruptEnabled = false;
    watchdog_counter = INITIAL_WATCHDOG;
    ctcVector = 0;
    frameCount = 0;
    /**
     * Homebrew BG scroll (real MCR-2 has none). NES-style: top-left of the screen
     * samples nametable at (scrollX, scrollY). Values are NES/logical pixels
     * (32×30 tile grid); rendering scales ×2 onto the 512×480 canvas.
     * scrollX is treated as signed 8-bit (e.g. 248 = -8).
     */
    scrollX = 0;
    scrollY = 0;

    audioadapter: TssChannelAdapter;
    psg1: AY38910_Audio;
    psg2: AY38910_Audio;

    constructor() {
        super();
        var audio = new MasterAudio();
        this.psg1 = new AY38910_Audio(audio);
        this.psg2 = new AY38910_Audio(audio);
        this.audioadapter = new TssChannelAdapter(
            [this.psg1.psg, this.psg2.psg], 2, this.sampleRate
        );

        this.connectCPUMemoryBus(this);
        this.connectCPUIOBus(this.newIOBus());
        this.inputs.set([0, 0, 0, 0xff, 0xff]); // inputs + DIP switches
        this.handler = newKeyboardHandler(this.inputs, MCR2_KEYCODE_MAP);
    }

    // Main CPU memory read
    read = newAddressDecoder([
        [0x0000, 0xDFFF, 0xFFFF, (a) => { return this.rom ? this.rom[a] : 0; }],
        [0xE000, 0xE7FF, 0x7FF, (a) => { return this.ram[a]; }],
        [0xE800, 0xEFFF, 0x1FF, (a) => { return this.sprram[a]; }],
        [0xF000, 0xF7FF, 0x7FF, (a) => { return this.vram[a]; }],
        [0xF800, 0xFFFF, 0x7F, (a) => { return this.palram[a]; }],
    ]);

    readConst(a: number): number | null {
        return this.read(a);
    }

    // Main CPU memory write
    write = newAddressDecoder([
        [0xE000, 0xE7FF, 0x7FF, (a, v) => { this.ram[a] = v; }],
        [0xE800, 0xEFFF, 0x1FF, (a, v) => { this.sprram[a] = v; }],
        [0xF000, 0xF7FF, 0x7FF, (a, v) => { this.vram[a] = v; }],
        [0xF800, 0xFFFF, 0x7F, (a, v) => {
            this.palram[a] = v;
            this.updatePalette(a);
        }],
    ]);

    // I/O bus
    newIOBus() {
        return {
            read: (addr: number) => {
                addr &= 0xFF;
                if (addr <= 0x04) {
                    return this.inputs[addr]; // SSIO input ports
                }
                if (addr >= 0x08 && addr <= 0x0F) {
                    return this.inputs[3]; // DIP switches
                }
                if (addr >= 0xF0 && addr <= 0xF3) {
                    return 0; // CTC read
                }
                // Homebrew: frame counter for vsync wait (increments each advanceFrame)
                if (addr == 0xF4) {
                    return this.frameCount & 0xff;
                }
                if (addr == 0xF5) return this.scrollY & 0xff;
                if (addr == 0xF6) return this.scrollX & 0xff;
                return 0;
            },
            write: (addr: number, val: number) => {
                addr &= 0xFF;
                if (addr == 0xE0) {
                    this.watchdog_counter = INITIAL_WATCHDOG;
                }
                if (addr >= 0xF0 && addr <= 0xF3) {
                    // Z80 CTC write — track IM2 vector; bit7 of control enables IRQ
                    if ((val & 1) == 0) {
                        this.ctcVector = val & 0xF8;
                    } else if (val & 0x80) {
                        this.interruptEnabled = true;
                    } else if ((val & 0xc0) == 0x00) {
                        this.interruptEnabled = false;
                    }
                }
                // Homebrew BG scroll (NES-style logical pixels). Real 91490 has none.
                if (addr == 0xF5) this.scrollY = val;
                if (addr == 0xF6) this.scrollX = val;
                // SSIO sound output (ports 0x00-0x07)
                if (addr >= 0x00 && addr <= 0x07) {
                    // Sound commands - ignored for now
                }
            }
        };
    }

    // Palette: MCR 9-bit format
    // From MAME: R = ((offset&1)<<2) | (pal>>6), G = pal&7, B = (pal>>3)&7
    // Each color entry = 2 bytes; only odd byte carries color data
    updatePalette(offset: number) {
        let i = offset >> 1;
        if (i >= 64) return;
        let pal = this.palram[i * 2 + 1]; // odd byte has color data
        let r = pal3bit((pal >> 6) & 3);        // R low 2 bits from pal[7:6]
        let g = pal3bit(pal & 7);                // G from pal[2:0]
        let b = pal3bit((pal >> 3) & 7);         // B from pal[5:3]
        // R MSB comes from even byte bit 0
        let r_msb = (this.palram[i * 2] & 1) << 2;
        r = pal3bit(r_msb | ((pal >> 6) & 3));
        this.palette[i] = 0xFF000000 | (b << 16) | (g << 8) | r;
    }

    // Draw one scanline
    drawScanline() {
        let sl = this.scanline;
        if (sl >= MCR2_NUM_VISIBLE_SCANLINES) return;

        let pixofs = sl * MCR2_CANVAS_WIDTH;

        // BG is stored at half vertical res and doubled (even+odd lines via drawTileLine).
        // Do NOT reuse the halved Y for sprites — that caused interlaced ghosts.
        // NES-style scroll: top of screen shows nametable at scrollY (logical px, ×2 to canvas).
        // Positive scrollY → title sits above the viewport and falls in as scrollY → 0.
        if ((sl & 1) == 0) {
            let srcSl = sl + (this.scrollY & 0xff) * 2;
            if (srcSl >= 0 && srcSl < MCR2_NUM_VISIBLE_SCANLINES) {
                let half = srcSl >> 1;
                let tileRow = Math.floor(half / MCR2_TILE_SIZE);
                let tileY = half % MCR2_TILE_SIZE;
                // signed scrollX in NES pixels → canvas; matches neslib scroll(x,y)
                let sx = (this.scrollX << 24) >> 24;
                let originX = sx * 2;
                let tileW = MCR2_TILE_SIZE * 2;
                let scrollTilesX = Math.floor(originX / tileW);
                let scrollPixX = ((originX % tileW) + tileW) % tileW;

                if (tileRow < MCR2_TILE_ROWS) {
                    // +1 column so fine scroll can pull in a partial tile at the edges
                    for (let tileCol = 0; tileCol <= MCR2_TILE_COLS; tileCol++) {
                        let srcCol = (tileCol + scrollTilesX) & 31;
                        let vramOfs = (tileRow * MCR2_TILE_COLS + srcCol) * 2;
                        let byte0 = this.vram[vramOfs];
                        let byte1 = this.vram[vramOfs + 1];

                        let tileCode = byte0 | ((byte1 & 0x03) << 8);
                        let tilePalette = (byte1 >> 4) & 0x03;
                        let flipX = (byte1 & 0x04) != 0;
                        let flipY = (byte1 & 0x08) != 0;

                        let ty = flipY ? (15 - tileY) : tileY;
                        let pixX = tileCol * tileW - scrollPixX;
                        if (pixX <= -16 || pixX >= MCR2_CANVAS_WIDTH) continue;
                        this.drawTileLine(pixofs + pixX, tileCode, ty, tilePalette, flipX);
                    }
                }
            } else {
                // Off-nametable (title scroll-in) — clear this line pair
                this.pixels.fill(this.palette[0] || 0xFF000000, pixofs, pixofs + MCR2_CANVAS_WIDTH * 2);
            }
        }

        this.drawSpriteScanline(sl, pixofs);
    }

    // Render one row of a 16×16 background tile (2 bitplanes)
    drawTileLine(outOfs: number, tileCode: number, row: number, palette: number, flipX: boolean) {
        let gfxBase = ROM_BG_GFX_START;
        let halfSize = ROM_BG_GFX_SIZE / 2; // 8KB per bitplane

        // Tile layout: 8 bytes per tile per bitplane (tight pack)
        let tileOfs = tileCode * 8;
        let byteOfs = tileOfs + (row & 7);

        let p0L = this.rom[gfxBase + byteOfs] || 0;
        let p1L = this.rom[gfxBase + halfSize + byteOfs] || 0;

        let colorBase = palette * 4;

        for (let x = 0; x < 8; x++) {
            let bit = 7 - x;
            let srcX = flipX ? (14 - x * 2) : x * 2;
            let color = ((p0L >> bit) & 1) | (((p1L >> bit) & 1) << 1);
            let px = outOfs + srcX;
            // outOfs may be mid-line with fine scrollX; clip to this scanline pair
            let lineBase = outOfs - (outOfs % MCR2_CANVAS_WIDTH);
            let xpix = px - lineBase;
            if (xpix < 0 || xpix + 1 >= MCR2_CANVAS_WIDTH) continue;
            let c = this.palette[colorBase + color];
            this.pixels[px] = this.pixels[px + 512] = this.pixels[px + 1] = this.pixels[px + 513] = c;
        }
    }

    // Render sprites intersecting a given scanline (91464 sprite board, 4bpp)
    drawSpriteScanline(scanline: number, pixofs: number) {
        let gfxBase = ROM_SPR_GFX_START;
        let planeSize = ROM_SPR_GFX_SIZE / 4; // 8KB per bitplane

        // Iterate sprites back-to-front (last sprite = highest priority)
        for (let sprNum = 31; sprNum >= 0; sprNum--) {
            let base = sprNum * 4;
            // MAME 91464: low-res sprite coords, then *2 to match 512×480 / 16×16 tiles
            let sy = ((241 - this.sprram[base]) * 2) & 0x1FF;
            let attrib = this.sprram[base + 1];
            let code = this.sprram[base + 2] | ((attrib & 0x08) ? 0x100 : 0);
            let sx = ((this.sprram[base + 3] - 3) * 2) & 0x1FF;

            let flipX = (attrib & 0x10) != 0;
            let flipY = (attrib & 0x20) != 0;
            let sprPalette = (attrib & 0x03);
            let colorBase = 16 + sprPalette * 4; // 4 sprite pals × 4 pens (NES-style)

            // Check scanline intersection with 32-pixel tall sprite
            let relY = scanline - sy;
            if (relY < 0 || relY >= 32) continue;

            let row = flipY ? (31 - relY) : relY;

            // Sprite GFX: 4 planes × 2KB; each sprite 128 bytes/plane, row-major
            // (4 bytes per row × 32 rows) — matches asset editor + gen_mcr_chase_gfx.py
            let sprOfs = code * 128 + row * 4;

            for (let col = 0; col < 4; col++) {
                let byteOfs = sprOfs + col;
                let p0 = this.rom[gfxBase + byteOfs] || 0;
                let p1 = this.rom[gfxBase + planeSize + byteOfs] || 0;
                let p2 = this.rom[gfxBase + planeSize * 2 + byteOfs] || 0;
                let p3 = this.rom[gfxBase + planeSize * 3 + byteOfs] || 0;

                for (let x = 0; x < 8; x++) {
                    let bit = 7 - x;
                    let color = ((p0 >> bit) & 1) |
                                (((p1 >> bit) & 1) << 1) |
                                (((p2 >> bit) & 1) << 2) |
                                (((p3 >> bit) & 1) << 3);
                    if (color == 0) continue; // transparent

                    let px: number;
                    if (flipX) {
                        px = sx + 31 - (col * 8 + x);
                    } else {
                        px = sx + col * 8 + x;
                    }

                    if (px >= 0 && px < MCR2_CANVAS_WIDTH) {
                        this.pixels[pixofs + px] = this.palette[colorBase + color];
                    }
                }
            }
        }
    }

    startScanline() {
        this.audio && this.audioadapter && this.audioadapter.generate(this.audio);
    }

    advanceFrame(trap) {
        var steps = super.advanceFrame(trap);

        this.frameCount = (this.frameCount + 1) & 0xff;

        // Watchdog
        if (this.watchdog_counter-- <= 0) {
            throw new EmuHalt("WATCHDOG FIRED");
        }

        // VBlank IRQ via CTC channel 0 (only once the game enables it)
        if (this.interruptEnabled) {
            this.cpu.interrupt(this.ctcVector);
        }

        return steps;
    }

    reset() {
        super.reset();
        this.watchdog_counter = INITIAL_WATCHDOG;
        this.interruptEnabled = false;
        this.ctcVector = 0;
        this.frameCount = 0;
        this.psg1.reset();
        this.psg2.reset();
    }

    loadROM(data) {
        this.rom = padBytes(data, this.defaultROMSize);
        // Rebuild palette
        for (let i = 0; i < 64; i++) {
            this.updatePalette(i * 2);
        }
    }

    loadState(state) {
        super.loadState(state);
        this.sprram.set(state.sprram);
        this.vram.set(state.vram);
        this.palram.set(state.palram);
        this.watchdog_counter = state.wdc;
        this.interruptEnabled = state.ie;
        this.ctcVector = state.ctcv;
        this.frameCount = state.fc || 0;
        for (let i = 0; i < 64; i++) {
            this.updatePalette(i * 2);
        }
    }

    saveState() {
        var state = super.saveState();
        state['sprram'] = this.sprram.slice(0);
        state['vram'] = this.vram.slice(0);
        state['palram'] = this.palram.slice(0);
        state['wdc'] = this.watchdog_counter;
        state['ie'] = this.interruptEnabled;
        state['ctcv'] = this.ctcVector;
        state['fc'] = this.frameCount;
        return state;
    }
}
