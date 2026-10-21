'use strict';
// Keyboard and controller bindings for the game (platform/pad/pad.cpp reads
// them from the file named by SMS_BINDINGS: one `CONTROL = KEY ... PAD_...`
// line per control; its keys replace the control's keys and its PAD_ buttons
// replace its controller buttons). The launcher keeps only the controls a
// player changed; everything else stays the game's default.
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.SmsBindings = factory();
})(typeof self !== 'undefined' ? self : this, () => {
  const CONTROLS = [
    { id: 'STICK_UP', label: 'Move forward' }, { id: 'STICK_DOWN', label: 'Move back' },
    { id: 'STICK_LEFT', label: 'Move left' }, { id: 'STICK_RIGHT', label: 'Move right' },
    { id: 'HALF_TILT', label: 'Walk (half tilt)' },
    { id: 'A', label: 'A button' }, { id: 'B', label: 'B button' }, { id: 'X', label: 'X button' },
    { id: 'Y', label: 'Y button' }, { id: 'Z', label: 'Z button' }, { id: 'L', label: 'L trigger' },
    { id: 'R', label: 'R trigger' }, { id: 'START', label: 'Start' },
    { id: 'CSTICK_UP', label: 'Camera up' }, { id: 'CSTICK_DOWN', label: 'Camera down' },
    { id: 'CSTICK_LEFT', label: 'Camera left' }, { id: 'CSTICK_RIGHT', label: 'Camera right' },
    { id: 'DPAD_UP', label: 'D-pad up' }, { id: 'DPAD_DOWN', label: 'D-pad down' },
    { id: 'DPAD_LEFT', label: 'D-pad left' }, { id: 'DPAD_RIGHT', label: 'D-pad right' },
    { id: 'QUIT', label: 'Quit the game' },
    { id: 'L_SOFT', label: 'Soft L press' }, { id: 'R_SOFT', label: 'Soft R press' }
  ];
  // The game's built-in defaults (kDefaultBindings in platform/pad/pad.cpp).
  const DEFAULTS = {
    A: ['SPACE', 'X'], B: ['LSHIFT', 'RSHIFT', 'C'], X: ['V'], Y: ['F'], Z: ['Z'], L: ['Q'], R: ['E'],
    START: ['ENTER'], DPAD_UP: ['1', 'KP_8'], DPAD_DOWN: ['2', 'KP_2'], DPAD_LEFT: ['3', 'KP_4'],
    DPAD_RIGHT: ['4', 'KP_6'], STICK_UP: ['UP', 'W'], STICK_DOWN: ['DOWN', 'S'], STICK_LEFT: ['LEFT', 'A'],
    STICK_RIGHT: ['RIGHT', 'D'], CSTICK_UP: ['I'], CSTICK_DOWN: ['K'], CSTICK_LEFT: ['J'], CSTICK_RIGHT: ['L'],
    HALF_TILT: ['LCTRL'], QUIT: ['ESCAPE'],
    // a light L or R press without the click (L_SOFT / R_SOFT in pad.cpp); unbound by default
    L_SOFT: [], R_SOFT: []
  };
  // KeyboardEvent.code to the key names the game knows (kKeys in pad.cpp).
  const CODES = {
    Enter: 'ENTER', Escape: 'ESCAPE', Backspace: 'BACKSPACE', Tab: 'TAB', Space: 'SPACE', Minus: 'MINUS',
    Equal: 'EQUALS', BracketLeft: 'LBRACKET', BracketRight: 'RBRACKET', Semicolon: 'SEMICOLON',
    Quote: 'APOSTROPHE', Comma: 'COMMA', Period: 'PERIOD', Slash: 'SLASH', F1: 'F1', F2: 'F2', F3: 'F3', F4: 'F4',
    ArrowUp: 'UP', ArrowDown: 'DOWN', ArrowLeft: 'LEFT', ArrowRight: 'RIGHT', NumpadDivide: 'KP_DIVIDE',
    NumpadMultiply: 'KP_MULTIPLY', NumpadSubtract: 'KP_MINUS', NumpadAdd: 'KP_PLUS', NumpadEnter: 'KP_ENTER',
    ControlLeft: 'LCTRL', ShiftLeft: 'LSHIFT', AltLeft: 'LALT', ControlRight: 'RCTRL', ShiftRight: 'RSHIFT',
    AltRight: 'RALT'
  };
  for (const letter of 'ABCDEFGHIJKLMNOPQRSTUVWXYZ') CODES[`Key${letter}`] = letter;
  for (let digit = 0; digit <= 9; digit++) { CODES[`Digit${digit}`] = String(digit); CODES[`Numpad${digit}`] = `KP_${digit}`; }
  const KEYS = new Set(Object.values(CODES));
  const MAX_KEYS = 8;  // kMaxKeys in pad.cpp

  // Controller buttons the game reads (kPadButtons in pad.cpp), named for an Xbox pad, in the order of the
  // browser Gamepad API's standard mapping (navigator.getGamepads(): button 0 is A ... 16 is Guide).
  const PAD_BUTTONS = [
    { id: 'PAD_A', label: 'A' }, { id: 'PAD_B', label: 'B' }, { id: 'PAD_X', label: 'X' }, { id: 'PAD_Y', label: 'Y' },
    { id: 'PAD_LB', label: 'LB' }, { id: 'PAD_RB', label: 'RB' }, { id: 'PAD_LT', label: 'LT' }, { id: 'PAD_RT', label: 'RT' },
    { id: 'PAD_BACK', label: 'Back' }, { id: 'PAD_START', label: 'Start' }, { id: 'PAD_LSTICK', label: 'Left stick press' },
    { id: 'PAD_RSTICK', label: 'Right stick press' }, { id: 'PAD_DPUP', label: 'D-pad up' }, { id: 'PAD_DPDOWN', label: 'D-pad down' },
    { id: 'PAD_DPLEFT', label: 'D-pad left' }, { id: 'PAD_DPRIGHT', label: 'D-pad right' }, { id: 'PAD_GUIDE', label: 'Guide' }
  ];
  const PADS = new Set(PAD_BUTTONS.map(button => button.id));
  const MAX_PADS = 4;  // kMaxPad in pad.cpp
  // The game's built-in controller layout (kDefaultPad in pad.cpp); other controls have no button.
  const PAD_DEFAULTS = {
    A: ['PAD_A'], B: ['PAD_B'], X: ['PAD_X'], Y: ['PAD_Y'], Z: ['PAD_RB'], L: ['PAD_LT'], R: ['PAD_RT'],
    START: ['PAD_START'], DPAD_UP: ['PAD_DPUP'], DPAD_DOWN: ['PAD_DPDOWN'], DPAD_LEFT: ['PAD_DPLEFT'],
    DPAD_RIGHT: ['PAD_DPRIGHT']
  };

  // The controller button for a standard-mapping Gamepad API button index, or null.
  function padFromIndex(index) { return PAD_BUTTONS[index]?.id || null; }
  function padLabel(id) { return PAD_BUTTONS.find(button => button.id === id)?.label || id; }

  // The key name for a KeyboardEvent.code, or null for a key the game cannot read.
  function keyFromCode(code) { return CODES[code] || null; }

  // Only the controls that differ from the defaults, each a list of known keys.
  function normalize(input) {
    const result = {};
    if (!input || typeof input !== 'object') return result;
    for (const { id } of CONTROLS) {
      if (!Array.isArray(input[id])) continue;
      const keys = [...new Set(input[id].map(String).filter(key => KEYS.has(key)))].slice(0, MAX_KEYS);
      if (keys.join(' ') !== DEFAULTS[id].join(' ')) result[id] = keys;
    }
    return result;
  }

  function keysFor(bindings, id) { return (bindings && bindings[id]) || DEFAULTS[id]; }

  // Only the controls whose controller buttons differ from the defaults, each a non-empty list of known buttons.
  function normalizePads(input) {
    const result = {};
    if (!input || typeof input !== 'object') return result;
    for (const { id } of CONTROLS) {
      if (!Array.isArray(input[id])) continue;
      const pads = [...new Set(input[id].map(String).filter(pad => PADS.has(pad)))].slice(0, MAX_PADS);
      if (pads.length && pads.join(' ') !== (PAD_DEFAULTS[id] || []).join(' ')) result[id] = pads;
    }
    return result;
  }

  function padsFor(padBindings, id) { return (padBindings && padBindings[id]) || PAD_DEFAULTS[id] || []; }

  // Every control's keys; controller buttons only for the controls changed (the rest keep the game's layout).
  function fileText(bindings, padBindings = {}) {
    const lines = ['# Written by SMS Launcher from Settings → Controls. Changes here are replaced on the next launch.'];
    for (const { id } of CONTROLS) {
      const pads = padBindings && padBindings[id] ? padBindings[id] : [];
      lines.push(`${id} =${[...keysFor(bindings, id), ...pads].map(item => ` ${item}`).join('')}`);
    }
    return `${lines.join('\n')}\n`;
  }

  return { CONTROLS, DEFAULTS, PAD_BUTTONS, PAD_DEFAULTS, keyFromCode, normalize, keysFor, fileText,
    padFromIndex, padLabel, normalizePads, padsFor };
});
