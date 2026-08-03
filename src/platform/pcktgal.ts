import { Base6502MachinePlatform, Platform } from "../common/baseplatform";
import { PLATFORMS } from "../common/emu";
import { PocketGalMachine } from "../machine/pcktgal";

const PCKTGAL_PRESETS = [
    { id: 'hello.c', name: 'Hello World', category: "C" },
    { id: 'sprites.c', name: 'Sprites / Tiles Demo', category: "C" },
    { id: 'spritetest.c', name: 'Sprite Hardware Test', category: "C" },
    { id: 'pacman.c', name: 'Pac-Man', category: "Games" },
];

class PocketGalPlatform extends Base6502MachinePlatform<PocketGalMachine> implements Platform {

    newMachine() { return new PocketGalMachine(); }
    getPresets() { return PCKTGAL_PRESETS; }
    readAddress(a) { return this.machine.readConst(a); }

    getMemoryMap() {
        return {
            main: [
                { name: 'Work RAM', start: 0x0000, size: 0x800, type: 'ram' },
                { name: 'Playfield VRAM', start: 0x0800, size: 0x800, type: 'ram' },
                { name: 'Sprite RAM', start: 0x1000, size: 0x200, type: 'ram' },
                { name: 'BAC06 / Inputs', start: 0x1800, size: 0x100, type: 'io' },
                { name: 'Banked ROM', start: 0x4000, size: 0x4000, type: 'rom' },
                { name: 'Program ROM', start: 0x8000, size: 0x8000, type: 'rom' },
            ]
        };
    }

    showHelp() {
        return "https://8bitworkshop.com/docs/platforms/arcade/index.html";
    }
}

PLATFORMS['pcktgal'] = PocketGalPlatform;
