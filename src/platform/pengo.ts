import { Platform } from "../common/baseplatform";
import { PLATFORMS } from "../common/emu";
import { PengoMachine } from "../machine/pengo";
import { BaseZ80MachinePlatform, getToolForFilename_z80 } from "../common/baseplatform";

const PENGO_PRESETS = [
  { id: 'hello.c', name: 'Hello World' },
  { id: 'chase.c', name: 'Chase' },
];

class PengoPlatform extends BaseZ80MachinePlatform<PengoMachine> implements Platform {

  newMachine()          { return new PengoMachine(); }
  getPresets()          { return PENGO_PRESETS; }
  getDefaultExtension() { return ".c"; };
  readAddress(a)        { return this.machine.readConst(a); }
  readVRAMAddress(a)    {
    if (a < 0x400) return this.machine.vram[a];
    else if (a < 0x800) return this.machine.cram[a - 0x400];
    else return this.machine.ram[0x7f0 + ((a - 0x800) & 0xf)];
  }

  getMemoryMap = function() { return { main:[
    {name:'Program ROM', start:0x0000,size:0x8000,type:'rom'},
    {name:'Video RAM',   start:0x8000,size:0x400, type:'ram'},
    {name:'Color RAM',   start:0x8400,size:0x400, type:'ram'},
    {name:'Work RAM',    start:0x8800,size:0x7f0, type:'ram'},
    {name:'Sprite RAM',  start:0x8ff0,size:0x10,  type:'ram'},
    {name:'I/O / Sound', start:0x9000,size:0x100, type:'io'},
    {name:'Color PROM',  start:0xc000,size:0x20,  type:'rom'},
    {name:'Lookup PROM', start:0xc100,size:0x400, type:'rom'},
    {name:'Wave ROM',    start:0xc500,size:0x100, type:'rom'},
  ] } };
  showHelp() { return "https://8bitworkshop.com/docs/platforms/arcade/index.html#pengo-hardware" }

  getDebugTree() {
    let tree = super.getDebugTree();
    tree['banks'] = {
      gfx: this.machine.gfx.gfxBank,
      palette: this.machine.gfx.paletteBank,
      colortable: this.machine.gfx.colorTableBank,
    };
    tree['sprites'] = {
      $$: () => {
        let spriteData = {};
        for (let i = 0; i < 8; i++) {
          let base = 0x7f0 + i * 2;
          spriteData[`sprite_${i}`] = {
            shape: '$' + this.machine.ram[base].toString(16).padStart(2, '0'),
            color: '$' + this.machine.ram[base + 1].toString(16).padStart(2, '0'),
            x: this.machine.gfx.spritePos[i * 2],
            y: this.machine.gfx.spritePos[i * 2 + 1],
          };
        }
        return spriteData;
      }
    };
    return tree;
  }
}

PLATFORMS['pengo'] = PengoPlatform;
