const { uIOhook } = require('uiohook-napi');
const os = require('os');

/**
 * Native monitor to fetch CPU and RAM without spawning PowerShell processes.
 */
class FastSystemMonitor {
  constructor() {
    this.prevCpuInfo = this._getCPUTicks();
  }

  _getCPUTicks() {
    const cpus = os.cpus();
    let totalIdle = 0;
    let totalTick = 0;
    cpus.forEach(cpu => {
      for (const type in cpu.times) {
        totalTick += cpu.times[type];
      }
      totalIdle += cpu.times.idle;
    });
    return { idle: totalIdle, total: totalTick };
  }

  getCPUUsage() {
    const currentCpuInfo = this._getCPUTicks();
    const idleDiff = currentCpuInfo.idle - this.prevCpuInfo.idle;
    const totalDiff = currentCpuInfo.total - this.prevCpuInfo.total;
    this.prevCpuInfo = currentCpuInfo;
    if (totalDiff === 0) return 0;
    return Math.round(100 * (1 - idleDiff / totalDiff));
  }

  getRAMUsage() {
    const total = os.totalmem();
    const free = os.freemem();
    return Math.round(((total - free) / total) * 100);
  }
}

const monitor = new FastSystemMonitor();

// mutable state object exported to callers
const state = {
  kb: false,
  mouseMove: false,
  mouseClick: false,
  lastInputTs: 0,
  cpu: 0,
  ram: 0
};

const KEY_LATCH_MS = 120;
let keyPhysicalDown = false;
let keyLatchTimer;

function markInputActivity() {
  state.lastInputTs = Date.now();
}

function updateKeyboardSignal() {
  if (keyPhysicalDown) {
    state.kb = true;
    return;
  }
  clearTimeout(keyLatchTimer);
  keyLatchTimer = setTimeout(() => {
    if (!keyPhysicalDown) state.kb = false;
  }, KEY_LATCH_MS);
}

// emitter for custom events (e.g. mouse stopped)
//const emitter = new EventEmitter();

// -------- Keyboard Logging --------
uIOhook.on('keydown', event => {
 //console.log("Key down");
 keyPhysicalDown = true;
 state.kb = true;
 markInputActivity();
});

uIOhook.on('keyup', event => {
  //console.log("Key up");
  keyPhysicalDown = false;
  markInputActivity();
  updateKeyboardSignal();
});

let stopTimer;
const STOP_DELAY = 150; // ms without movement to consider "stopped"

uIOhook.on('mousemove', event => {
 //console.log("Mouse move");
  state.mouseMove = true;
  markInputActivity();
 // reset/arm the stop timer
 clearTimeout(stopTimer);
 stopTimer = setTimeout(() => {

   state.mouseMove = false;

 }, STOP_DELAY);
});

uIOhook.on('mousedown', event => {
 // console.log("Mouse down");
 state.mouseClick = true;
 markInputActivity();
});

uIOhook.on('mouseup', event => {
 //console.log("Mouse up");
  state.mouseClick = false;
  markInputActivity();
});

uIOhook.start();

// Update system stats every 1 second natively
setInterval(() => {
  state.cpu = monitor.getCPUUsage();
  state.ram = monitor.getRAMUsage();
  // Note: These values are now accessible to anyone importing this 'state' object
}, 1000);


// export the mutable state object (and any helpers if needed)
module.exports = state;
