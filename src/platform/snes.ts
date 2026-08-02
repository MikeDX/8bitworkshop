import { Platform, Preset, getToolForFilename_6502 } from "../common/baseplatform";
import { PLATFORMS, RasterVideo } from "../common/emu";
import { Emulator, defaultInputMap } from "snes9x2005-wasm";

const SNES_PRESETS: Preset[] = [
  { id: "hello.c", name: "Hello World (C)" },
  { id: "mode7.c", name: "Mode 7 Demo (C)" },
  { id: "hello.wiz", name: "Hello World (Wiz)" },
];

class SNESPlatform implements Platform {
  mainElement: HTMLElement;
  video: RasterVideo;
  emu: Emulator | null = null;
  lastROM: Uint8Array | null = null;
  keyHandlers: { onkeydown: (e: KeyboardEvent) => void; onkeyup: (e: KeyboardEvent) => void } | null = null;

  constructor(mainElement: HTMLElement) {
    this.mainElement = mainElement;
  }

  async start() {
    this.video = new RasterVideo(this.mainElement, 256, 224, { aspect: 8 / 7 });
    this.video.create();
    this.emu = await Emulator.create(this.video.canvas, {
      wasmPath: "res/snes9x_2005.wasm",
      audioOn: false,
    });
    this.keyHandlers = this.emu.createKeyboardHandles(defaultInputMap, false);
    window.addEventListener("keydown", this.keyHandlers.onkeydown);
    window.addEventListener("keyup", this.keyHandlers.onkeyup);
  }

  reset() {
    if (this.lastROM && this.emu) {
      this.emu.loadRom(this.lastROM, true);
    }
  }

  isRunning() {
    return this.emu?.isRunning() ?? false;
  }

  pause() {
    this.emu?.pauseEmulation();
  }

  resume() {
    this.emu?.startEmulation();
  }

  getPresets() {
    return SNES_PRESETS;
  }

  loadROM(_title: string, rom: Uint8Array) {
    this.lastROM = rom;
    if (this.emu) {
      this.emu.loadRom(rom, true);
    }
  }

  getPlatformName() {
    return "Super NES";
  }

  getToolForFilename(fn: string) {
    if (fn.endsWith(".c") || fn.endsWith(".h")) return "snes-c";
    return getToolForFilename_6502(fn);
  }

  getDefaultExtensions() {
    return [".c", ".wiz"];
  }
}

PLATFORMS["snes"] = SNESPlatform;
