'use strict';
// Button prompts: the images the game draws for the button glyphs in its text
// (platform gx_prompts.cpp in the game). For each style the launcher draws one
// strip of eight 64x64 icons, in the order A B X Y Z L R C-stick, saved as
// <style>.png in the folder named by SMS_BUTTON_PROMPT_DIR. Each icon shows what
// that GameCube button is bound to (src/bindings.js), so a remapped key or
// controller button shows as the player set it.
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory(require('./bindings'));
  else root.SmsPrompts = factory(root.SmsBindings);
})(typeof self !== 'undefined' ? self : this, bindings => {
  const CELL = 64;
  const GLYPHS = ['A', 'B', 'X', 'Y', 'Z', 'L', 'R', 'CSTICK'];
  // The choices for the setting; gamecube keeps the game's own glyphs and auto
  // follows the device in use (keyboard and mouse, or a controller). The others
  // always show that style, whatever is plugged in.
  const STYLES = [
    { id: 'gamecube', label: 'Original (GameCube)' },
    { id: 'auto', label: 'Automatic' },
    { id: 'xbox', label: 'Xbox' }, { id: 'playstation', label: 'PlayStation 5' },
    { id: 'steamdeck', label: 'Steam Deck' }, { id: 'keyboard', label: 'PC keyboard and mouse' }
  ];
  // With auto, how a controller's prompts look: like that controller (match) or
  // always one style, such as PlayStation prompts on an Xbox controller.
  const PAD_STYLES = [
    { id: 'match', label: 'Match my controller' }, { id: 'xbox', label: 'Xbox' },
    { id: 'playstation', label: 'PlayStation 5' }, { id: 'steamdeck', label: 'Steam Deck' },
    { id: 'gamecube', label: 'Original (GameCube)' }
  ];
  // The styles that have images (auto picks among them while playing).
  const DRAWN = ['xbox', 'playstation', 'steamdeck', 'keyboard'];

  function normalizeStyle(value) { return STYLES.some(style => style.id === value) ? value : 'gamecube'; }
  function normalizePad(value) { return PAD_STYLES.some(style => style.id === value) ? value : 'match'; }

  // How each controller button looks in each style. Face buttons are circles
  // with a letter or a PlayStation symbol, shoulders and triggers are tabs.
  const FACE = {
    xbox: {
      PAD_A: { text: 'A', fill: '#107c10' }, PAD_B: { text: 'B', fill: '#d0271d' },
      PAD_X: { text: 'X', fill: '#1d66d0' }, PAD_Y: { text: 'Y', fill: '#e2a90b' }
    },
    playstation: {
      PAD_A: { symbol: 'cross', fill: '#22252b', ink: '#7ca7e8' }, PAD_B: { symbol: 'circle', fill: '#22252b', ink: '#e8707a' },
      PAD_X: { symbol: 'square', fill: '#22252b', ink: '#da8cc6' }, PAD_Y: { symbol: 'triangle', fill: '#22252b', ink: '#5fc7a6' }
    },
    steamdeck: {
      PAD_A: { text: 'A', fill: '#2b3138' }, PAD_B: { text: 'B', fill: '#2b3138' },
      PAD_X: { text: 'X', fill: '#2b3138' }, PAD_Y: { text: 'Y', fill: '#2b3138' }
    }
  };
  const OTHER = {
    xbox: { PAD_LB: 'LB', PAD_RB: 'RB', PAD_LT: 'LT', PAD_RT: 'RT', PAD_BACK: 'View', PAD_START: 'Menu', PAD_LSTICK: 'LS', PAD_RSTICK: 'RS', PAD_GUIDE: 'Xbox' },
    playstation: { PAD_LB: 'L1', PAD_RB: 'R1', PAD_LT: 'L2', PAD_RT: 'R2', PAD_BACK: 'Create', PAD_START: 'Options', PAD_LSTICK: 'L3', PAD_RSTICK: 'R3', PAD_GUIDE: 'PS' },
    steamdeck: { PAD_LB: 'L1', PAD_RB: 'R1', PAD_LT: 'L2', PAD_RT: 'R2', PAD_BACK: 'View', PAD_START: 'Menu', PAD_LSTICK: 'L3', PAD_RSTICK: 'R3', PAD_GUIDE: 'Steam' }
  };
  const DPAD = { PAD_DPUP: 'up', PAD_DPDOWN: 'down', PAD_DPLEFT: 'left', PAD_DPRIGHT: 'right' };

  // Short labels for keys whose game name is long (the rest show as named).
  const KEY_LABELS = {
    SPACE: 'Space', ENTER: 'Enter', ESCAPE: 'Esc', BACKSPACE: 'Bksp', TAB: 'Tab', LSHIFT: 'Shift', RSHIFT: 'Shift',
    LCTRL: 'Ctrl', RCTRL: 'Ctrl', LALT: 'Alt', RALT: 'Alt', UP: '↑', DOWN: '↓', LEFT: '←', RIGHT: '→',
    MINUS: '-', EQUALS: '=', LBRACKET: '[', RBRACKET: ']', SEMICOLON: ';', APOSTROPHE: "'", COMMA: ',',
    PERIOD: '.', SLASH: '/', KP_DIVIDE: 'Num/', KP_MULTIPLY: 'Num*', KP_MINUS: 'Num-', KP_PLUS: 'Num+', KP_ENTER: 'NumEnt'
  };
  function keyLabel(key) { return KEY_LABELS[key] || key.replace(/^KP_/, 'Num'); }

  // What to draw for one glyph: a key cap, a mouse, a controller button, the
  // right stick, or (nothing bound) the GameCube letter greyed out.
  function icon(style, glyph, keyBindings, padBindings, options = {}) {
    if (style === 'keyboard') {
      if (glyph === 'CSTICK') {
        if (options.mouseCamera) return { kind: 'mouse' };
        const keys = ['CSTICK_UP', 'CSTICK_LEFT', 'CSTICK_DOWN', 'CSTICK_RIGHT'].map(id => bindings.keysFor(keyBindings, id)[0]);
        if (keys.every(key => key && keyLabel(key).length === 1)) return { kind: 'key', text: keys.map(keyLabel).join('') };
        return keys[0] ? { kind: 'key', text: keyLabel(keys[0]) } : { kind: 'none', text: 'C' };
      }
      const key = bindings.keysFor(keyBindings, glyph)[0];
      return key ? { kind: 'key', text: keyLabel(key) } : { kind: 'none', text: glyph };
    }
    if (glyph === 'CSTICK') return { kind: 'stick', text: style === 'xbox' ? 'RS' : 'R' };
    const pad = bindings.padsFor(padBindings, glyph)[0];
    if (!pad) return { kind: 'none', text: glyph };
    if (FACE[style][pad]) return { kind: 'face', ...FACE[style][pad] };
    if (DPAD[pad]) return { kind: 'dpad', direction: DPAD[pad] };
    const text = OTHER[style][pad] || bindings.padLabel(pad);
    if (/^(LB|RB|L1|R1)$/.test(text)) return { kind: 'shoulder', text };
    if (/^(LT|RT|L2|R2)$/.test(text)) return { kind: 'trigger', text };
    return { kind: 'pill', text };
  }

  function icons(style, keyBindings, padBindings, options) {
    return GLYPHS.map(glyph => icon(style, glyph, keyBindings, padBindings, options));
  }

  // ---- drawing (a CanvasRenderingContext2D; the browser side only)

  function roundRect(ctx, x, y, w, h, r) {
    ctx.beginPath();
    ctx.moveTo(x + r, y);
    ctx.arcTo(x + w, y, x + w, y + h, r);
    ctx.arcTo(x + w, y + h, x, y + h, r);
    ctx.arcTo(x, y + h, x, y, r);
    ctx.arcTo(x, y, x + w, y, r);
    ctx.closePath();
  }

  // Text centred at (cx, cy), shrunk to fit width.
  function label(ctx, text, cx, cy, size, width, colour = '#fff') {
    let px = size;
    ctx.font = `700 ${px}px "Segoe UI", system-ui, sans-serif`;
    while (px > 9 && ctx.measureText(text).width > width) ctx.font = `700 ${--px}px "Segoe UI", system-ui, sans-serif`;
    ctx.fillStyle = colour;
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillText(text, cx, cy + px * 0.05);
  }

  function outlined(ctx, fill, stroke = '#0b0d10') {
    ctx.fillStyle = fill;
    ctx.fill();
    ctx.lineWidth = 3;
    ctx.strokeStyle = stroke;
    ctx.stroke();
  }

  function symbol(ctx, kind, cx, cy, colour) {
    ctx.strokeStyle = colour;
    ctx.lineWidth = 4.5;
    ctx.lineCap = 'round';
    ctx.lineJoin = 'round';
    ctx.beginPath();
    if (kind === 'cross') { ctx.moveTo(cx - 10, cy - 10); ctx.lineTo(cx + 10, cy + 10); ctx.moveTo(cx + 10, cy - 10); ctx.lineTo(cx - 10, cy + 10); }
    if (kind === 'circle') ctx.arc(cx, cy, 11, 0, Math.PI * 2);
    if (kind === 'square') ctx.rect(cx - 10, cy - 10, 20, 20);
    if (kind === 'triangle') { ctx.moveTo(cx, cy - 12); ctx.lineTo(cx + 12, cy + 9); ctx.lineTo(cx - 12, cy + 9); ctx.closePath(); }
    ctx.stroke();
  }

  function drawIcon(ctx, item, x) {
    const cx = x + CELL / 2, cy = CELL / 2;
    ctx.save();
    switch (item.kind) {
    case 'face':
      ctx.beginPath(); ctx.arc(cx, cy, 27, 0, Math.PI * 2); outlined(ctx, item.fill);
      if (item.symbol) symbol(ctx, item.symbol, cx, cy, item.ink);
      else label(ctx, item.text, cx, cy, 34, 40);
      break;
    case 'shoulder':
      roundRect(ctx, x + 4, 14, CELL - 8, 36, 12); outlined(ctx, '#3a4049');
      label(ctx, item.text, cx, cy, 26, CELL - 16);
      break;
    case 'trigger':
      ctx.beginPath(); ctx.moveTo(x + 8, 56); ctx.lineTo(x + 8, 20); ctx.quadraticCurveTo(x + 8, 6, cx, 6);
      ctx.quadraticCurveTo(x + CELL - 8, 6, x + CELL - 8, 20); ctx.lineTo(x + CELL - 8, 56); ctx.closePath();
      outlined(ctx, '#3a4049');
      label(ctx, item.text, cx, 34, 26, CELL - 20);
      break;
    case 'pill':
      roundRect(ctx, x + 3, 16, CELL - 6, 32, 16); outlined(ctx, '#3a4049');
      label(ctx, item.text, cx, cy, 22, CELL - 14);
      break;
    case 'stick':
      ctx.beginPath(); ctx.arc(cx, cy, 27, 0, Math.PI * 2); outlined(ctx, '#3a4049');
      ctx.beginPath(); ctx.arc(cx, cy, 17, 0, Math.PI * 2); ctx.fillStyle = '#23272d'; ctx.fill();
      label(ctx, item.text, cx, cy, 22, 30);
      break;
    case 'dpad': {
      ctx.beginPath();
      ctx.moveTo(x + 24, 6); ctx.lineTo(x + 40, 6); ctx.lineTo(x + 40, 24); ctx.lineTo(x + 58, 24); ctx.lineTo(x + 58, 40);
      ctx.lineTo(x + 40, 40); ctx.lineTo(x + 40, 58); ctx.lineTo(x + 24, 58); ctx.lineTo(x + 24, 40); ctx.lineTo(x + 6, 40);
      ctx.lineTo(x + 6, 24); ctx.lineTo(x + 24, 24); ctx.closePath();
      outlined(ctx, '#3a4049');
      const at = { up: [cx, 15], down: [cx, 49], left: [x + 15, cy], right: [x + 49, cy] }[item.direction];
      ctx.beginPath(); ctx.arc(at[0], at[1], 6, 0, Math.PI * 2); ctx.fillStyle = '#fff'; ctx.fill();
      break;
    }
    case 'key':
      roundRect(ctx, x + 4, 6, CELL - 8, CELL - 12, 9); outlined(ctx, '#f2f4f6', '#1b1f24');
      roundRect(ctx, x + 9, 10, CELL - 18, CELL - 24, 6); ctx.fillStyle = '#ffffff'; ctx.fill();
      label(ctx, item.text, cx, 29, item.text.length > 2 ? 22 : 30, CELL - 16, '#1b1f24');
      break;
    case 'mouse':
      roundRect(ctx, x + 16, 4, 32, 56, 16); outlined(ctx, '#f2f4f6', '#1b1f24');
      ctx.beginPath(); ctx.moveTo(cx, 6); ctx.lineTo(cx, 24); ctx.moveTo(x + 17, 24); ctx.lineTo(x + 47, 24);
      ctx.lineWidth = 3; ctx.strokeStyle = '#1b1f24'; ctx.stroke();
      break;
    default:
      ctx.beginPath(); ctx.arc(cx, cy, 25, 0, Math.PI * 2); outlined(ctx, '#7b828c');
      label(ctx, item.text, cx, cy, 30, 40);
    }
    ctx.restore();
  }

  // One style's strip of icons on a new canvas (document.createElement).
  function drawStrip(doc, style, keyBindings, padBindings, options) {
    const canvas = doc.createElement('canvas');
    canvas.width = CELL * GLYPHS.length;
    canvas.height = CELL;
    const ctx = canvas.getContext('2d');
    icons(style, keyBindings, padBindings, options).forEach((item, index) => drawIcon(ctx, item, index * CELL));
    return canvas;
  }

  return { CELL, GLYPHS, STYLES, PAD_STYLES, DRAWN, normalizeStyle, normalizePad, keyLabel, icon, icons, drawStrip };
});
