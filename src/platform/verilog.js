"use strict";

var VERILOG_PRESETS = [
  {id:'clock_divider.v', name:'Clock Divider'},
  {id:'hvsync_generator.v', name:'Video Sync Generator'},
  {id:'test_hvsync.v', name:'Test Pattern'},
  {id:'lfsr.v', name:'Linear Feedback Shift Register'},
  {id:'digits10.v', name:'Digits'},
  {id:'ball_slip_counter.v', name:'Ball Motion (slipping counter)'},
  {id:'ball_paddle.v', name:'Brick Smash Game'},
  //{id:'pong.v', name:'Pong'},
];

var VERILOG_KEYCODE_MAP = makeKeycodeMap([
  [Keys.VK_LEFT, 0, 0x1],
  [Keys.VK_RIGHT, 0, 0x2],
  [Keys.VK_UP, 0, 0x4],
  [Keys.VK_DOWN, 0, 0x8],
  [Keys.VK_SPACE, 0, 0x10],
  [Keys.VK_SHIFT, 0, 0x20],
  [Keys.VK_1, 0, 0x40],
  [Keys.VK_2, 0, 0x80],
  [Keys.VK_5, 0, 0x100],
  [Keys.VK_6, 0, 0x200],
  [Keys.VK_7, 0, 0x400],
]);

  var VL_UL = function(x) { return x; }

  /// Return true if data[bit] set
  var VL_BITISSET_I = this.VL_BITISSET_I = function(data,bit) { return (data & (VL_UL(1)<<VL_BITBIT_I(bit))); }

  var VL_EXTENDSIGN_I = this.VL_EXTENDSIGN_I = function(lbits, lhs) { return (-((lhs)&(VL_UL(1)<<(lbits-1)))); }

  var VL_EXTEND_II = this.VL_EXTEND_II = function(obits,lbits,lhs) { return lhs; }

  var VL_EXTENDS_II = this.VL_EXTENDS_II = function(x,lbits,lhs) {
    return VL_EXTENDSIGN_I(lbits,lhs) | lhs;
  }

  var VL_EXTEND_II = this.VL_EXTEND_II = function(obits,lbits,lhs) { return lhs; }

  var VL_NEGATE_I = this.VL_NEGATE_I = function(x) { return -x; }

  var VL_LTS_III = this.VL_LTS_III = function(x,lbits,y,lhs,rhs) {
    return VL_EXTENDS_II(x,lbits,lhs) < VL_EXTENDS_II(x,lbits,rhs); }

  var VL_GTS_III = this.VL_GTS_III = function(x,lbits,y,lhs,rhs) {
    return VL_EXTENDS_II(x,lbits,lhs) > VL_EXTENDS_II(x,lbits,rhs); }

  var VL_LTES_III = this.VL_LTES_III = function(x,lbits,y,lhs,rhs) {
    return VL_EXTENDS_II(x,lbits,lhs) <= VL_EXTENDS_II(x,lbits,rhs); }

  var VL_GTES_III = this.VL_GTES_III = function(x,lbits,y,lhs,rhs) {
    return VL_EXTENDS_II(x,lbits,lhs) >= VL_EXTENDS_II(x,lbits,rhs); }

//

function VerilatorBase() {

  var VL_RAND_RESET_I = this.VL_RAND_RESET_I = function(bits) { return Math.floor(Math.random() * (1<<bits)); }

  //

  function vl_fatal(msg) {
    console.log(msg);
  }

  var RESET_TICKS = 1000;

  this.reset2 = function() {
    if (this.reset !== undefined) {
      this.reset = 0;
      this.tick2();
      this.reset = 1;
      for (var i=0; i<RESET_TICKS; i++)
        this.tick2();
      this.reset = 0;
    }
  }

  this.tick2 = function() {
    this.clk = 0;
    this.eval();
    this.clk = 1;
    this.eval();
  }

  var vlSymsp = {TOPp:this};
  var maxVclockLoop = 1;

  this.eval = function() {
    vlSymsp.TOPp = this;
    // Initialize
    if (!vlSymsp.__Vm_didInit)
      this._eval_initial_loop(vlSymsp);
    // Evaluate till stable
    //VL_DEBUG_IF(VL_PRINTF("\n----TOP Evaluate Vmain::eval\n"); );
    var __VclockLoop = 0;
    var __Vchange=1;
    while (__Vchange) {
        //VL_DEBUG_IF(VL_PRINTF(" Clock loop\n"););
        vlSymsp.__Vm_activity = true;
        this._eval(vlSymsp);
        __Vchange = this._change_request(vlSymsp);
        if (++__VclockLoop > 100) { vl_fatal("Verilated model didn't converge"); }
    }
    if (__VclockLoop > maxVclockLoop) {
      maxVclockLoop = __VclockLoop;
      console.log("Graph took " + maxVclockLoop + " iterations to stabilize");
    }
  }

  this._eval_initial_loop = function(vlSymsp) {
    vlSymsp.__Vm_didInit = true;
    this._eval_initial(vlSymsp);
    vlSymsp.__Vm_activity = true;
    var __VclockLoop = 0;
    var __Vchange=1;
    while (__Vchange) {
        this._eval_settle(vlSymsp);
        this._eval(vlSymsp);
        __Vchange = this._change_request(vlSymsp);
        if (++__VclockLoop > 100) { vl_fatal("Verilated model didn't DC converge"); }
    }
  }
}

var VerilogPlatform = function(mainElement, options) {
  var self = this;
  var video, audio;
  var videoWidth  = 288;
  var videoHeight = 248;
  var idata, timer;
  var gen;
  var frameRate = 60;
  var AUDIO_FREQ = 15750;
  var current_output;
  var paddle_x = 0;
  var paddle_y = 0;
  var switches = [0];

  this.getPresets = function() { return VERILOG_PRESETS; }

  var RGBLOOKUP = [
    0xff111111,
    0xff1111ff,
    0xff11ff11,
    0xff11ffff,
    0xffff1111,
    0xffff11ff,
    0xffffff11,
    0xffffffff,
  ];

  function vidtick() {
    gen.tick2();
    audio.addSingleSample(0+gen.spkr); // TODO: sync with audio freq
  }

  function updateVideoFrame() {
    var i=0;
    for (var y=0; y<videoHeight; y++) {
      gen.hpaddle = y > paddle_x ? 1 : 0;
      gen.vpaddle = y > paddle_y ? 1 : 0;
      for (var x=0; x<videoWidth; x++) {
        vidtick();
        idata[i++] = RGBLOOKUP[gen.rgb];
      }
      var z=0;
      while (gen.hsync && z++<videoWidth) vidtick();
      while (!gen.hsync && z++<videoWidth) vidtick();
    }
    var z=0;
    while (gen.vsync && z++<videoWidth*80) vidtick();
    while (!gen.vsync && z++<videoWidth*80) vidtick();
    video.updateFrame();
  }

  var yposlist = [];
  var lasty = [];

  function updateScopeFrame() {
    var arr = current_output.ports;
    if (!arr) return;
    for (var i=0; i<idata.length; i++) {
      if (idata[i])
        idata[i] = 0; //<<= 1;
    }
    var COLOR_SIGNAL = 0xff11ff11;
    var COLOR_BORDER = 0xff661111;
    var COLOR_TRANS_SIGNAL = 0xff116611;
    for (var x=0; x<videoWidth; x++) {
      gen.clk ^= 1;
      gen.eval();
      var yb = 8;
      var y1 = 0;
      for (var i=0; i<arr.length; i++) {
        var v = arr[i];
        var lo = 0; // TODO? v.ofs?
        var hi = v.len ? ((2 << v.len)-1) : 1;
        var ys = hi>1 ? v.len*2+8 : 8;
        var y2 = y1+ys;
        var z = gen[v.name];
        var y = Math.round(y2 - ys*((z-lo)/hi));
        yposlist[i] = y2;
        var ly = lasty[i];
        if (x > 0 && ly != y) {
          var dir = ly < y ? 1 : -1;
          while ((ly += dir) != y && ly >= y1 && ly <= y2) {
            idata[x + ly*videoWidth] = COLOR_TRANS_SIGNAL;
          }
        }
        lasty[i] = y;
        //idata[x + y1*videoWidth] = COLOR_BORDER;
        //idata[x + y2*videoWidth] = COLOR_BORDER;
        idata[x + y*videoWidth] = COLOR_SIGNAL;
        y1 += ys+yb;
      }
    }
    video.updateFrame();
    // draw labels
    var ctx = video.getContext();
    ctx.font = "8px TinyFont";
    ctx.fillStyle = "white";
    for (var i=0; i<arr.length; i++) {
      var v = arr[i];
      ctx.textAlign = 'left';
      ctx.fillText(v.name, 1, yposlist[i]);
      //ctx.textAlign = 'right';
      //ctx.fillText(""+gen[v.name], videoWidth-1, yposlist[i]);
    }
  }

  function clamp(minv,maxv,v) {
    return (v < minv) ? minv : (v > maxv) ? maxv : v;
  }

  this.start = function() {
    // TODO
    video = new RasterVideo(mainElement,videoWidth,videoHeight);
    video.create();
    setKeyboardFromMap(video, switches, VERILOG_KEYCODE_MAP);
		$(video.canvas).mousemove(function(e) {
			paddle_x = clamp(4,255,Math.floor(e.offsetX * video.canvas.width / $(video.canvas).width() - 20));
			paddle_y = clamp(4,255,Math.floor(e.offsetY * video.canvas.height / $(video.canvas).height() - 20));
		});
    audio = new SampleAudio(AUDIO_FREQ);
    idata = video.getFrameData();
    // TODO: 15.7 kHz?
    timer = new AnimationTimer(frameRate, function() {
			if (!self.isRunning())
				return;
      gen.switches = switches[0];
      if (gen.vsync !== undefined && gen.hsync !== undefined && gen.rgb !== undefined)
        updateVideoFrame();
      else
        updateScopeFrame();
    });
  }

  function printErrorCodeContext(e, code) {
    if (e.lineNumber && e.message) {
      var lines = code.split('\n');
      var s = e.message + '\n';
      for (var i=0; i<lines.length; i++) {
        if (i > e.lineNumber-5 && i < e.lineNumber+5) {
          s += lines[i] + '\n';
        }
      }
      console.log(s);
    }
  }

  this.loadROM = function(title, output) {
    var mod;
    try {
      mod = new Function('base', output.code);
    } catch (e) {
      printErrorCodeContext(e, output.code);
      throw e;
    }
    var base = new VerilatorBase();
    gen = new mod(base);
    gen.__proto__ = base;
    output.code = null;
    output.ports_signals = output.ports.concat(output.signals);
    current_output = output;
    this.reset();
  }

  this.isRunning = function() {
    return timer && timer.isRunning();
  }
  this.pause = function() {
    timer.stop();
    audio.stop();
  }
  this.resume = function() {
    timer.start();
    audio.start();
  }

  this.reset = function() {
    gen._ctor_var_reset();
    gen.reset2();
  }
  this.getToolForFilename = function(fn) {
    return "verilator";
  }
  this.getDefaultExtension = function() { return ".v"; };
};

////////////////

PLATFORMS['verilog'] = VerilogPlatform;