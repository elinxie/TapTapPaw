const path = require('path')
let iohook
try {
  iohook = require('iohook-macos')
} catch (e) {
  // Fallback: If running from src/core but dependencies are in macos/node_modules
  // we resolve the path explicitly.
  iohook = require(path.resolve(__dirname, '../../macos/node_modules/iohook-macos'))
}

// mutable state object exported to callers
const state = {
  kb: false,
  mouseMove: false,
  mouseClick: false,
  lastInputTs: 0
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

// Check accessibility permissions first

const permissions = iohook.checkAccessibilityPermissions()
if (!permissions.hasPermissions) {
    console.log('Accessibility permissions required')
    iohook.requestAccessibilityPermissions()

}

// Keyboard events
iohook.on('keyDown', (event) => {
   // console.log('Key pressed:', event.keyCode)
     keyPhysicalDown = true;
     state.kb = true;
     markInputActivity();
})

iohook.on('keyUp', (event) => {
   // console.log('Key released:', event.keyCode)
     keyPhysicalDown = false;
     markInputActivity();
     updateKeyboardSignal();
})

let mouseMoveTimeout;
const MOUSE_STOP_DELAY = 150; // ms

iohook.on('mouseMoved', (event) => {
   // console.log('Mouse moved to:', event.x, event.y)
   state.mouseMove = true;
   markInputActivity();
   clearTimeout(mouseMoveTimeout);
   mouseMoveTimeout = setTimeout(() => {
       state.mouseMove = false;
   }, MOUSE_STOP_DELAY);
})

iohook.on('leftMouseDown', (event) => {
    //console.log('Left click at:', event.x, event.y)
    state.mouseClick = true;
    markInputActivity();
})

iohook.on('leftMouseUp', (event) => {
    //console.log('Left click released at:', event.x, event.y)
    state.mouseClick = false;
    markInputActivity();
})

// Note: iohook-macos does not support scroll events reliably

// Enable performance mode
iohook.enablePerformanceMode()

// Set optimal polling rate (60fps)
iohook.setPollingRate(16)

// Throttle high-frequency mouse events
iohook.setMouseMoveThrottling(16)

// Monitor queue size
setInterval(() => {
    const queueSize = iohook.getQueueSize()
    if (queueSize > 100) {
        console.warn('Queue getting large:', queueSize)
    }
    // Do not log `state` continuously; frequent console output interferes
    // with interactive prompts (e.g. `readline.question`) on macOS.
    // This interval is now only for monitoring queue size if needed.
    // Mouse move state is handled by the timeout.
}, 1000) // Check less frequently

// Start monitoring
try {
  iohook.startMonitoring();
} catch (err) {
  console.error("Accessibility permission missing.");
  console.error("Please enable TapTapPaw in System Settings → Privacy & Security → Accessibility.");
}

//------------------------------------------------------
module.exports = state;
