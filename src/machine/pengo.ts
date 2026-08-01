import { Z80 } from "../common/cpu/ZilogZ80";
import { BasicScanlineMachine } from "../common/devices";
import { padBytes, Keys, makeKeycodeMap, newKeyboardHandler, EmuHalt } from "../common/emu";

/**
 * Pengo (Sega) — Pac-Man hardware family with remapped memory, 32KB program ROM,
 * dual gfx banks, and a larger color lookup PROM. Memory map matches MAME pengo.cpp
 * (non-encrypted / pengou).
 *
 * Combined 8bitworkshop ROM layout:
 *   0x0000-0x7FFF  program (32KB)
 *   0x8000-0x8FFF  tile ROM bank 0
 *   0x9000-0x9FFF  tile ROM bank 1
 *   0xA000-0xAFFF  sprite ROM bank 0
 *   0xB000-0xBFFF  sprite ROM bank 1
 *   0xC000-0xC01F  color PROM (32)
 *   0xC100-0xC4FF  color lookup (1024)
 *   0xC500-0xC5FF  wave ROM (256)
 */

const PENGO_KEYCODE_MAP = makeKeycodeMap([
    [Keys.UP,    0, 0x1],
    [Keys.DOWN,  0, 0x2],
    [Keys.LEFT,  0, 0x4],
    [Keys.RIGHT, 0, 0x8],
    [Keys.VK_5,  0, 0x10], // COIN1
    [Keys.VK_6,  0, 0x20], // COIN2
    [Keys.A,     0, 0x80], // BUTTON1 / FIRE
    [Keys.START, 1, 0x20], // START1
    [Keys.VK_2,  1, 0x40], // START2
]);

const SCREEN_W = 224;
const SCREEN_H = 288;

class PengoVideo {
    tiles = new Uint8Array(512 * 64);      // 2 banks × 256 tiles
    sprites = new Uint8Array(128 * 256);   // 2 banks × 64 sprites
    colors = new Uint32Array(32);
    paletteRom = new Uint8Array(0x400);
    spritePos = new Uint8Array(16);
    gfxBank = 0;
    paletteBank = 0;
    colorTableBank = 0;

    constructor(
        public rom: Uint8Array,
        public vram: Uint8Array,
        public cram: Uint8Array,
        public ram: Uint8Array,
    ) {
        this.rebuild();
    }

    rebuild() {
        for (var i = 0; i < 32; i++) {
            var d = this.rom[0xc000 + i];
            var r = ((d >> 0) & 1) * 0x21 + ((d >> 1) & 1) * 0x47 + ((d >> 2) & 1) * 0x97;
            var g = ((d >> 3) & 1) * 0x21 + ((d >> 4) & 1) * 0x47 + ((d >> 5) & 1) * 0x97;
            var b = ((d >> 6) & 1) * 0x51 + ((d >> 7) & 1) * 0xae;
            this.colors[i] = 0xff000000 | (b << 16) | (g << 8) | r;
        }
        this.paletteRom.set(this.rom.subarray(0xc100, 0xc500));

        for (var bank = 0; bank < 2; bank++) {
            var tileRom = this.rom.subarray(0x8000 + bank * 0x1000, 0x9000 + bank * 0x1000);
            for (var t = 0; t < 256; t++) {
                var out = this.tiles.subarray((bank * 256 + t) * 64, (bank * 256 + t) * 64 + 64);
                var src = tileRom.subarray(t * 16, t * 16 + 16);
                this.decodeStrip(src, 0, out, 0, 4, 8);
                this.decodeStrip(src, 8, out, 0, 0, 8);
            }
            var sprRom = this.rom.subarray(0xa000 + bank * 0x1000, 0xb000 + bank * 0x1000);
            for (var s = 0; s < 64; s++) {
                var sout = this.sprites.subarray((bank * 64 + s) * 256, (bank * 64 + s) * 256 + 256);
                var ssrc = sprRom.subarray(s * 64, s * 64 + 64);
                this.decodeStrip(ssrc, 0 * 8, sout, 8, 12, 16);
                this.decodeStrip(ssrc, 1 * 8, sout, 8, 0, 16);
                this.decodeStrip(ssrc, 2 * 8, sout, 8, 4, 16);
                this.decodeStrip(ssrc, 3 * 8, sout, 8, 8, 16);
                this.decodeStrip(ssrc, 4 * 8, sout, 0, 12, 16);
                this.decodeStrip(ssrc, 5 * 8, sout, 0, 0, 16);
                this.decodeStrip(ssrc, 6 * 8, sout, 0, 4, 16);
                this.decodeStrip(ssrc, 7 * 8, sout, 0, 8, 16);
            }
        }
    }

    decodeStrip(input: Uint8Array, inOff: number, output: Uint8Array, bx: number, by: number, imgWidth: number) {
        var base = by * imgWidth + bx;
        for (var x = 0; x < 8; x++) {
            var strip = input[inOff + x];
            for (var y = 0; y < 4; y++) {
                var i = (3 - y) * imgWidth + (7 - x);
                var pen = ((strip >> y) & 1) | (((strip >> (y + 4)) & 1) << 1);
                output[base + i] = pen;
            }
        }
    }

    /** Resolve CRAM/sprite color byte through Pengo bank bits (MAME pacman_v.cpp). */
    getPalette(palNo: number, out: Uint8Array) {
        var idx = (palNo & 0x1f) | (this.colorTableBank << 5);
        var o = (idx & 0x3f) * 4;
        for (var i = 0; i < 4; i++) {
            var pen = this.paletteRom[o + i] & 0x0f;
            if (this.paletteBank) pen |= 0x10;
            out[i] = pen;
        }
    }

    drawTile(pixels: Uint32Array, tileNo: number, pal: Uint8Array, x: number, y: number) {
        if (x < 0 || x >= SCREEN_W) return;
        var tile = this.tiles.subarray(tileNo * 64, tileNo * 64 + 64);
        for (var i = 0; i < 64; i++) {
            var px = i & 7;
            var py = i >> 3;
            var sx = x + px;
            if (sx < 0 || sx >= SCREEN_W) continue;
            var pen = pal[tile[i] & 3];
            pixels[(y + py) * SCREEN_W + sx] = this.colors[pen & 31];
        }
    }

    drawSprite(pixels: Uint32Array, spriteNo: number, pal: Uint8Array, x: number, y: number, flipX: boolean, flipY: boolean) {
        if (x <= -16 || x > SCREEN_W) return;
        var spr = this.sprites.subarray(spriteNo * 256, spriteNo * 256 + 256);
        for (var i = 0; i < 256; i++) {
            var px = i & 15;
            var py = i >> 4;
            var penIdx = spr[i] & 3;
            if (pal[penIdx] === 0) continue;
            var xPos = flipX ? 15 - px : px;
            var yPos = flipY ? 15 - py : py;
            var sx = x + xPos;
            var sy = y + yPos;
            if (sx < 0 || sx >= SCREEN_W || sy < 0 || sy >= SCREEN_H) continue;
            pixels[sy * SCREEN_W + sx] = this.colors[pal[penIdx] & 31];
        }
    }

    drawFrame(pixels: Uint32Array) {
        pixels.fill(0xff000000);
        var pal = new Uint8Array(4);
        var tileBase = this.gfxBank ? 256 : 0;
        var sprBase = this.gfxBank ? 64 : 0;

        var addr = 0;
        for (var y = 34; y < 36; y++) {
            for (var x = 31; x >= 0; x--) {
                this.getPalette(this.cram[addr], pal);
                this.drawTile(pixels, tileBase + this.vram[addr], pal, (x - 2) * 8, y * 8);
                addr++;
            }
        }

        addr = 0x40;
        for (var x = 29; x >= 2; x--) {
            for (var y = 2; y <= 33; y++) {
                this.getPalette(this.cram[addr], pal);
                this.drawTile(pixels, tileBase + this.vram[addr], pal, (x - 2) * 8, y * 8);
                addr++;
            }
        }

        addr = 0x3c0;
        for (var y = 0; y < 2; y++) {
            for (var x = 31; x >= 0; x--) {
                this.getPalette(this.cram[addr], pal);
                this.drawTile(pixels, tileBase + this.vram[addr], pal, (x - 2) * 8, y * 8);
                addr++;
            }
        }

        // Sprite attrs at 0x8FF0 → ram offset 0x7F0; coords at 0x9020
        for (var s = 7; s >= 0; s--) {
            var info = this.ram[0x7f0 + s * 2];
            var palNo = this.ram[0x7f0 + s * 2 + 1];
            var sx = SCREEN_W - this.spritePos[s * 2] + 15;
            var sy = SCREEN_H - this.spritePos[s * 2 + 1] - 16;
            this.getPalette(palNo, pal);
            this.drawSprite(pixels, sprBase + (info >> 2), pal, sx, sy, !!(info & 2), !!(info & 1));
        }
    }
}

const XTAL = 18432000.0;
const cpuFrequency = XTAL / 6;
const hsyncFrequency = XTAL / 3 / 192 / 2;
const cpuCyclesPerLine = cpuFrequency / hsyncFrequency;
const INITIAL_WATCHDOG = 256;

export class PengoMachine extends BasicScanlineMachine {

    cpuFrequency = cpuFrequency;
    canvasWidth = SCREEN_W;
    numTotalScanlines = SCREEN_H;
    numVisibleScanlines = SCREEN_H;
    defaultROMSize = 0x10000;
    cpuCyclesPerLine = cpuCyclesPerLine | 0;
    sampleRate = 60 * 264;
    rotate = 0;

    cpu: Z80 = new Z80();
    /** Work RAM 0x8800-0x8FFF (2KB including sprite attrs at 0x8FF0). */
    ram = new Uint8Array(0x800);
    vram = new Uint8Array(0x400);
    cram = new Uint8Array(0x400);
    oram = new Uint8Array(0x100);
    gfx: PengoVideo;

    interruptEnabled = 0;
    interruptVector = 0xff;
    pendingVBlankIsr = false;
    watchdog_counter = INITIAL_WATCHDOG;
    flipScreen = 0;
    soundEnabled = 0;
    soundRegs = new Uint8Array(0x20);
    soundAcc = new Uint32Array(3);
    waveforms: Uint8Array = PengoMachine.WAVEFORMS;
    keyMap = PENGO_KEYCODE_MAP;
    frameDrawn = false;

    static WAVEFORMS = PengoMachine.buildWaveforms();

    static buildWaveforms(): Uint8Array {
        var waves = new Uint8Array(8 * 32);
        for (var s = 0; s < 32; s++) {
            var t = s / 32;
            waves[0 * 32 + s] = (8 + Math.sin(t * Math.PI * 2) * 7.5) | 0;
            waves[1 * 32 + s] = s < 16 ? 15 : 0;
            waves[2 * 32 + s] = s < 16 ? s : (31 - s);
            waves[3 * 32 + s] = (s >> 1);
            waves[4 * 32 + s] = s < 8 ? 15 : (s < 16 ? 0 : (s < 24 ? 10 : 0));
            waves[5 * 32 + s] = (8 + Math.sin(t * Math.PI * 4) * 7.5) | 0;
            waves[6 * 32 + s] = s & 1 ? 12 : 2;
            waves[7 * 32 + s] = ((s * 3) & 15);
        }
        return waves;
    }

    constructor() {
        super();
        this.cpu.connectIOBus({
            read: (_p) => 0xff,
            write: (port, val) => { if ((port & 0xff) === 0) this.interruptVector = val & 0xff; }
        });
        this.cpu.connectMemoryBus({ read: this.readByte, write: this.writeByte });
        this.cpu.retryInterrupts = true;
        this.rom = new Uint8Array(this.defaultROMSize);
        this.gfx = new PengoVideo(this.rom, this.vram, this.cram, this.ram);
        this.inputs.fill(0);
        this.handler = newKeyboardHandler(this.inputs, this.keyMap);
    }

    readByte = (a: number): number => {
        a &= 0xffff;
        if (a < 0x8000) return this.rom[a];
        if (a < 0x8400) return this.vram[a - 0x8000];
        if (a < 0x8800) return this.cram[a - 0x8400];
        if (a < 0x9000) return this.ram[a - 0x8800];
        if (a < 0x9040) return 0xff; // DSW1
        if (a < 0x9080) return 0xc9; // DSW0
        if (a < 0x90c0) return (~this.inputs[1]) & 0xff; // IN1
        if (a < 0x9100) return (~this.inputs[0]) & 0xff; // IN0
        return 0xff;
    }

    writeByte = (a: number, v: number): void => {
        a &= 0xffff;
        if (a < 0x8000) return;
        if (a < 0x8400) { this.vram[a - 0x8000] = v; return; }
        if (a < 0x8800) { this.cram[a - 0x8400] = v; return; }
        if (a < 0x9000) { this.ram[a - 0x8800] = v; return; }
        if (a >= 0x9000 && a <= 0x901f) {
            this.soundRegs[a - 0x9000] = v & 0x0f;
            return;
        }
        if (a >= 0x9020 && a <= 0x902f) {
            this.gfx.spritePos[a - 0x9020] = v;
            return;
        }
        if (a >= 0x9040 && a <= 0x9047) {
            var bit = a & 7;
            var on = v & 1;
            if (bit === 0) this.interruptEnabled = on;
            else if (bit === 1) this.soundEnabled = on;
            else if (bit === 2) this.gfx.paletteBank = on;
            else if (bit === 3) this.flipScreen = on;
            else if (bit === 6) this.gfx.colorTableBank = on;
            else if (bit === 7) this.gfx.gfxBank = on;
            return;
        }
        if (a === 0x9070) {
            this.watchdog_counter = INITIAL_WATCHDOG;
        }
    }

    reset() {
        super.reset();
        this.cpu.reset();
        this.watchdog_counter = INITIAL_WATCHDOG;
        this.interruptEnabled = 0;
        this.interruptVector = 0xff;
        this.pendingVBlankIsr = false;
        this.soundEnabled = 0;
        this.soundRegs.fill(0);
        this.soundAcc.fill(0);
        this.gfx.gfxBank = 0;
        this.gfx.paletteBank = 0;
        this.gfx.colorTableBank = 0;
        this.extractROMData();
    }

    loadROM(data) {
        this.rom.set(padBytes(data, this.defaultROMSize));
        this.extractROMData();
    }

    extractROMData() {
        this.gfx = new PengoVideo(this.rom, this.vram, this.cram, this.ram);
        var custom = this.rom.subarray(0xc500, 0xc600);
        var hasCustom = false;
        for (var i = 0; i < custom.length; i++) {
            if (custom[i]) { hasCustom = true; break; }
        }
        this.waveforms = hasCustom ? custom : PengoMachine.WAVEFORMS;
    }

    advanceCPU() { return this.cpu.advanceInsn(); }

    private voiceFreq(v: number): number {
        var r = this.soundRegs;
        if (v === 0) {
            return r[0x10] | (r[0x11] << 4) | (r[0x12] << 8) | (r[0x13] << 12) | (r[0x14] << 16);
        }
        if (v === 1) {
            return (r[0x16] | (r[0x17] << 4) | (r[0x18] << 8) | (r[0x19] << 12)) << 4;
        }
        return (r[0x1b] | (r[0x1c] << 4) | (r[0x1d] << 8) | (r[0x1e] << 12)) << 4;
    }

    private voiceVol(v: number): number {
        if (v === 0) return this.soundRegs[0x15];
        if (v === 1) return this.soundRegs[0x1a];
        return this.soundRegs[0x1f];
    }

    private voiceWave(v: number): number {
        if (v === 0) return this.soundRegs[0x05] & 7;
        if (v === 1) return this.soundRegs[0x0a] & 7;
        return this.soundRegs[0x0f] & 7;
    }

    startScanline() {
        if (!this.audio || !this.soundEnabled) return;
        var steps = 6;
        var sum = 0;
        for (var step = 0; step < steps; step++) {
            var mix = 0;
            for (var v = 0; v < 3; v++) {
                var vol = this.voiceVol(v);
                if (!vol) continue;
                var freq = this.voiceFreq(v);
                if (!freq) continue;
                this.soundAcc[v] = (this.soundAcc[v] + freq) & 0xfffff;
                var idx = (this.soundAcc[v] >> 15) & 31;
                var samp = this.waveforms[this.voiceWave(v) * 32 + idx] & 0x0f;
                mix += (samp - 8) * vol;
            }
            sum += mix;
        }
        this.audio.feedSample(sum / (steps * 3 * 15 * 8), 1);
    }

    drawScanline() {
        if (this.scanline === 0) {
            this.gfx.drawFrame(this.pixels);
        }
    }

    private drainVBlankIsr() {
        if (!this.pendingVBlankIsr) return;
        this.pendingVBlankIsr = false;
        var spDone = this.cpu.getSP() + 2;
        var guard = 100000;
        while (this.cpu.getSP() !== spDone && --guard > 0) {
            this.advanceCPU();
        }
    }

    advanceFrame(trap) {
        this.drainVBlankIsr();
        var steps = super.advanceFrame(trap);
        if (--this.watchdog_counter <= 0) {
            throw new EmuHalt("WATCHDOG FIRED");
        }
        if (this.interruptEnabled) {
            var spBefore = this.cpu.getSP();
            this.cpu.interrupt(this.interruptVector);
            if (this.cpu.getSP() !== spBefore) {
                this.pendingVBlankIsr = true;
            }
        }
        return steps;
    }

    loadState(state) {
        this.cpu.loadState(state.c);
        this.ram.set(state.ram);
        this.vram.set(state.vr);
        this.cram.set(state.cr);
        this.gfx.spritePos.set(state.sp || []);
        this.watchdog_counter = state.wdc;
        this.interruptEnabled = state.ie;
        this.pendingVBlankIsr = !!state.pvi;
        this.gfx.gfxBank = state.gb || 0;
        this.gfx.paletteBank = state.pb || 0;
        this.gfx.colorTableBank = state.cb || 0;
        this.loadControlsState(state);
    }

    saveState() {
        return {
            c: this.cpu.saveState(),
            ram: this.ram.slice(0),
            vr: this.vram.slice(0),
            cr: this.cram.slice(0),
            or: this.oram.slice(0),
            sp: this.gfx.spritePos.slice(0),
            wdc: this.watchdog_counter,
            ie: this.interruptEnabled,
            pvi: this.pendingVBlankIsr,
            gb: this.gfx.gfxBank,
            pb: this.gfx.paletteBank,
            cb: this.gfx.colorTableBank,
            inputs: this.inputs.slice(0),
        };
    }

    read(a: number) { return this.readByte(a); }
    write(a: number, v: number) { this.writeByte(a, v); }
    readConst(a: number) { return this.readByte(a); }
}
