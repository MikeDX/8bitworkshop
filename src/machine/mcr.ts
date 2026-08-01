// based on https://raw.githubusercontent.com/mamedev/mame/master/src/mame/bally/mcr.cpp
// license:BSD-3-Clause
// copyright-holders:Aaron Giles
//
// Aligned with MAME 91490 / `timber`: no BG scroll, active-low SSIO,
// MAME mcr_bg_layout + mcr_sprite_layout gfx packing in the download blob.

import { Z80, Z80State } from "../common/cpu/ZilogZ80";
import { BasicScanlineMachine } from "../common/devices";
import { KeyFlags, newAddressDecoder, padBytes, Keys, makeKeycodeMap, newKeyboardHandler, EmuHalt } from "../common/emu";
import { MasterAudio, AY38910_Audio, TssChannelAdapter } from "../common/audio";

const MCR2_XTAL = 19968000;
const MCR2_CPU_FREQ = MCR2_XTAL / 4; // ~4.992 MHz
const MCR2_NUM_VISIBLE_SCANLINES = 480;
const MCR2_NUM_TOTAL_SCANLINES = 525;
const MCR2_CANVAS_WIDTH = 512;
const MCR2_FPS = 30;
const MCR2_CYCLES_PER_LINE = Math.floor(MCR2_CPU_FREQ / (MCR2_NUM_TOTAL_SCANLINES * MCR2_FPS));

const MCR2_TILE_COLS = 32;
const MCR2_TILE_ROWS = 30;
const MCR2_TILE_SIZE = 8; // logical pixels before ×2 display

const INITIAL_WATCHDOG = 16;

function pal3bit(v: number): number {
    v &= 7;
    return (v << 5) | (v << 2) | (v >> 1);
}

/* timber IP bit order; negative mask = active low */
const MCR2_KEYCODE_MAP = makeKeycodeMap([
    [Keys.SELECT, 0, -0x1],   // Coin 1
    [Keys.VK_5, 0, -0x1],     // Coin 1 (alt)
    [Keys.START, 0, -0x4],    // 1P Start
    [Keys.VK_1, 0, -0x4],     // 1P Start (alt)
    [Keys.VK_2, 0, -0x8],     // 2P Start
    [Keys.RIGHT, 1, -0x1],    // P1 Right
    [Keys.LEFT, 1, -0x2],     // P1 Left
    [Keys.DOWN, 1, -0x4],     // P1 Down
    [Keys.UP, 1, -0x8],       // P1 Up
    [Keys.A, 1, -0x10],       // P1 Button 1 (Space)
    [Keys.GP_A, 1, -0x10],    // P1 Button 1 (X)
    [Keys.B, 1, -0x20],       // P1 Button 2
    [Keys.GP_B, 1, -0x20],    // P1 Button 2 (Z)
    [Keys.P2_RIGHT, 2, -0x1],
    [Keys.P2_LEFT, 2, -0x2],
    [Keys.P2_DOWN, 2, -0x4],
    [Keys.P2_UP, 2, -0x8],
    [Keys.P2_A, 2, -0x10],
    [Keys.P2_B, 2, -0x20],
]);

/*
 * Download blob (converter splits into timber ROM files):
 *   0x0000-0x7FFF  program
 *   0x8000-0x9FFF  BG gfx1 window: low half + high half (mcr_bg_layout)
 *   0xA000-0xBFFF  sprite gfx2 window: 4 quarters (mcr_sprite_layout)
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
    ram = new Uint8Array(0x800);
    sprram = new Uint8Array(0x200);
    vram = new Uint8Array(0x800);
    palram = new Uint8Array(0x80);

    palette = new Uint32Array(64);

    interruptEnabled = false;
    watchdog_counter = INITIAL_WATCHDOG;
    ctcVector = 0;
    /** Bit N set → next write to CTC channel N is a time constant, not control */
    ctcTimeConstFollows = 0;
    frameCount = 0;

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
        // Active-low inputs: 0xFF = none pressed
        this.inputs.set([0xff, 0xff, 0xff, 0xff, 0xff]);
        this.handler = newKeyboardHandler(this.inputs, MCR2_KEYCODE_MAP);
    }

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

    write = newAddressDecoder([
        [0xE000, 0xE7FF, 0x7FF, (a, v) => { this.ram[a] = v; }],
        [0xE800, 0xEFFF, 0x1FF, (a, v) => { this.sprram[a] = v; }],
        [0xF000, 0xF7FF, 0x7FF, (a, v) => { this.vram[a] = v; }],
        [0xF800, 0xFFFF, 0x7F, (a, v) => {
            this.palram[a] = v;
            // MAME mcr_paletteram9_w: 9-bit color from data | (A0<<8)
            let i = a >> 1;
            if (i >= 64) return;
            let value = v | ((a & 1) << 8);
            let r = pal3bit(value >> 6);
            let g = pal3bit(value >> 0);
            let b = pal3bit(value >> 3);
            this.palette[i] = 0xFF000000 | (b << 16) | (g << 8) | r;
        }],
    ]);

    newIOBus() {
        return {
            read: (addr: number) => {
                addr &= 0xFF;
                // SSIO IP0-IP4 (mirrored every 0x08 in low nibble group)
                let ip = addr & 0x07;
                if (ip <= 0x04) {
                    return this.inputs[ip];
                }
                if (addr >= 0xF0 && addr <= 0xF3) {
                    return 0;
                }
                // Unpulled SSIO bits read high (active-low idle)
                return 0xff;
            },
            write: (addr: number, val: number) => {
                addr &= 0xFF;
                if (addr == 0xE0) {
                    this.watchdog_counter = INITIAL_WATCHDOG;
                }
                if (addr >= 0xF0 && addr <= 0xF3) {
                    // Z80 CTC: after a control word with "time constant follows"
                    // (bit 2), the next write to that channel is the down-count
                    // value — not another control word.
                    let ch = addr - 0xF0;
                    let chMask = 1 << ch;
                    if (this.ctcTimeConstFollows & chMask) {
                        this.ctcTimeConstFollows &= ~chMask;
                        return;
                    }
                    if ((val & 1) == 0) {
                        // Bit0=0: interrupt vector (written to CH0)
                        this.ctcVector = val & 0xF8;
                    } else {
                        // Bit0=1: control word
                        if (val & 0x04) this.ctcTimeConstFollows |= chMask;
                        if (val & 0x80) this.interruptEnabled = true;
                        else this.interruptEnabled = false;
                    }
                }
            }
        };
    }

    updatePalette(offset: number) {
        // Re-apply as if the higher-priority byte was written last (see set_color).
        let i = offset >> 1;
        if (i >= 64) return;
        let even = this.palram[i * 2];
        let odd = this.palram[i * 2 + 1];
        let value: number;
        if (odd != 0) value = odd | 0x100;
        else value = even;
        let r = pal3bit(value >> 6);
        let g = pal3bit(value >> 0);
        let b = pal3bit(value >> 3);
        this.palette[i] = 0xFF000000 | (b << 16) | (g << 8) | r;
    }

    drawScanline() {
        let sl = this.scanline;
        if (sl >= MCR2_NUM_VISIBLE_SCANLINES) return;

        let pixofs = sl * MCR2_CANVAS_WIDTH;

        // BG at half vertical res, doubled via drawTileLine (even+odd).
        if ((sl & 1) == 0) {
            let half = sl >> 1;
            let tileRow = Math.floor(half / MCR2_TILE_SIZE);
            let tileY = half % MCR2_TILE_SIZE;

            if (tileRow < MCR2_TILE_ROWS) {
                for (let tileCol = 0; tileCol < MCR2_TILE_COLS; tileCol++) {
                    let vramOfs = (tileRow * MCR2_TILE_COLS + tileCol) * 2;
                    let byte0 = this.vram[vramOfs];
                    let byte1 = this.vram[vramOfs + 1];

                    let tileCode = byte0 | ((byte1 & 0x03) << 8);
                    let tilePalette = (byte1 >> 4) & 0x03;
                    let flipX = (byte1 & 0x04) != 0;
                    let flipY = (byte1 & 0x08) != 0;

                    let ty = flipY ? (15 - tileY) : tileY;
                    let pixX = tileCol * MCR2_TILE_SIZE * 2;
                    this.drawTileLine(pixofs + pixX, tileCode, ty, tilePalette, flipX);
                }
            }
        }

        this.drawSpriteScanline(sl, pixofs);
    }

    /** Decode one row of an 8×8 tile from MAME mcr_bg_layout (displayed ×2). */
    drawTileLine(outOfs: number, tileCode: number, row: number, palette: number, flipX: boolean) {
        let gfxBase = ROM_BG_GFX_START;
        let halfSize = ROM_BG_GFX_SIZE / 2; // 0x1000
        // Low half @ +0 → pens 0-3 (timber); high @ +halfSize → pens 4-15
        let tileOfs = tileCode * 16;
        let rowBits = (row & 7) * 16;

        let colorBase = palette * 16; // MAME 4bpp: 16 pens per tile palette bank

        for (let x = 0; x < 8; x++) {
            // mcr_bg_layout: STEP8(0,2); MAME digfx bit0 = MSB of byte
            let bit0 = rowBits + x * 2;
            let color = 0;
            // pens 0,1 from low half (timbg1)
            for (let p = 0; p < 2; p++) {
                let b = bit0 + p;
                let byte = this.rom[gfxBase + tileOfs + (b >> 3)] || 0;
                if (byte & (0x80 >> (b & 7))) color |= 1 << p;
            }
            // pens 2,3 from high half (timbg0)
            for (let p = 0; p < 2; p++) {
                let b = bit0 + p;
                let byte = this.rom[gfxBase + halfSize + tileOfs + (b >> 3)] || 0;
                if (byte & (0x80 >> (b & 7))) color |= 1 << (p + 2);
            }

            let srcX = flipX ? (14 - x * 2) : x * 2;
            let px = outOfs + srcX;
            let c = this.palette[colorBase + color];
            this.pixels[px] = this.pixels[px + 512] = this.pixels[px + 1] = this.pixels[px + 513] = c;
        }
    }

    /** Decode sprite scanline from MAME mcr_sprite_layout (4 quarters). */
    drawSpriteScanline(scanline: number, pixofs: number) {
        let gfxBase = ROM_SPR_GFX_START;
        let quarterSize = ROM_SPR_GFX_SIZE / 4; // 0x800

        for (let sprNum = 31; sprNum >= 0; sprNum--) {
            let base = sprNum * 4;
            let sy = ((241 - this.sprram[base]) * 2) & 0x1FF;
            let attrib = this.sprram[base + 1];
            let code = this.sprram[base + 2] | ((attrib & 0x08) ? 0x100 : 0);
            let sx = ((this.sprram[base + 3] - 3) * 2) & 0x1FF;

            let flipX = (attrib & 0x10) != 0;
            let flipY = (attrib & 0x20) != 0;
            // MAME 91464: ((~attrib & 3) << 4) & 0x30
            let sprPalette = (attrib & 0x03);
            let colorBase = ((~sprPalette & 3) << 4) & 0x30;

            let relY = scanline - sy;
            if (relY < 0 || relY >= 32) continue;

            let row = flipY ? (31 - relY) : relY;
            let sprBase = code * 128; // bytes per sprite per quarter

            for (let x = 0; x < 32; x++) {
                let x_group = x >> 1;
                let frac = x_group & 3;
                let pair = x_group >> 2;
                let bit_base = row * 32 + pair * 8 + ((x & 1) << 2);
                let color = 0;
                let qbase = gfxBase + frac * quarterSize + sprBase;
                for (let p = 0; p < 4; p++) {
                    let b = bit_base + p;
                    let byte = this.rom[qbase + (b >> 3)] || 0;
                    if (byte & (0x80 >> (b & 7))) color |= 1 << p;
                }
                if (color == 0) continue;

                let px = flipX ? (sx + 31 - x) : (sx + x);
                if (px >= 0 && px < MCR2_CANVAS_WIDTH) {
                    this.pixels[pixofs + px] = this.palette[colorBase + color];
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

        if (this.watchdog_counter-- <= 0) {
            throw new EmuHalt("WATCHDOG FIRED");
        }

        // VBlank IRQ (CTC CH3 on real hw). IM2 vector = (base & 0xF8) | (ch << 1).
        if (this.interruptEnabled) {
            this.cpu.interrupt((this.ctcVector & 0xF8) | (3 << 1));
        }

        return steps;
    }

    reset() {
        super.reset();
        this.watchdog_counter = INITIAL_WATCHDOG;
        this.interruptEnabled = false;
        this.ctcVector = 0;
        this.ctcTimeConstFollows = 0;
        this.frameCount = 0;
        this.inputs.set([0xff, 0xff, 0xff, 0xff, 0xff]);
        this.psg1.reset();
        this.psg2.reset();
    }

    loadROM(data) {
        this.rom = padBytes(data, this.defaultROMSize);
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
