'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const { zoomFor, displayZoom, scaledSize, followDisplayScale } = require('../src/display-scale');

const SIZE = { width: 1080, height: 760, minWidth: 900, minHeight: 600 };
const WM_DPICHANGED = 0x02E0, WM_ENTERSIZEMOVE = 0x0231, WM_EXITSIZEMOVE = 0x0232;
const tick = () => new Promise(resolve => setImmediate(resolve));
const monitor = (width, height, scaleFactor) =>
  ({ scaleFactor, bounds: { x: 0, y: 0, width: Math.round(width / scaleFactor), height: Math.round(height / scaleFactor) } });

function launcherWindow(scaleFactor, bounds, pixelHeight = 1440) {
  const hooks = {}, zooms = [], resizes = [];
  const display = { ...monitor(pixelHeight * 16 / 9, pixelHeight, scaleFactor), workArea: { x: 0, y: 0, width: 1707, height: 960 } };
  const window = { min: null, maximized: false, minimized: false, fullScreen: false, bounds,
    webContents: { setZoomFactor: zoom => zooms.push(zoom), on: (event, callback) => { hooks[event] = callback; } },
    hookWindowMessage: (message, callback) => { hooks[message] = callback; },
    on: (event, callback) => { hooks[event] = callback; },
    once() {}, isDestroyed: () => false, isMaximized: () => window.maximized, isFullScreen: () => window.fullScreen,
    isMinimized: () => window.minimized,
    getBounds: () => ({ ...window.bounds }), setBounds: value => { window.bounds = value; resizes.push(value); },
    setMinimumSize: (width, height) => { window.min = [width, height]; } };
  const switchDisplay = (scale, height) => Object.assign(display, monitor(height * 16 / 9, height, scale));
  followDisplayScale(window, { getDisplayMatching: () => display, on() {}, off() {} }, SIZE, displayZoom(display));
  return { window, zooms, resizes, hooks,
    switchDisplay,
    async changeScale(scale, height = 1440) {
      switchDisplay(scale, height);
      hooks[WM_DPICHANGED]();
      await tick();
    } };
}

const effectiveScale = display => displayZoom(display) * display.scaleFactor;

test('Full HD and smaller displays look as they do at 125% at most', () => {
  for (const scale of [1, 1.25]) assert.equal(displayZoom(monitor(1920, 1080, scale)), 1);
  assert.equal(displayZoom(monitor(1920, 1080, 1.5)), 1.25 / 1.5);
  for (const scale of [1.75, 2]) assert.ok(Math.abs(effectiveScale(monitor(1920, 1080, scale)) - 1.25) < 1e-9);
  assert.ok(Math.abs(effectiveScale(monitor(1600, 900, 1.5)) - 1.25) < 1e-9);
  assert.equal(displayZoom(monitor(1080, 1920, 1.5)), 1.25 / 1.5);
});

test('displays above Full HD look as they do at 155% at most', () => {
  for (const scale of [1, 1.5, 1.55]) assert.equal(displayZoom(monitor(2560, 1440, scale)), 1);
  for (const scale of [2.25, 2.5]) assert.ok(Math.abs(effectiveScale(monitor(2560, 1440, scale)) - 1.55) < 1e-9);
  assert.ok(Math.abs(effectiveScale(monitor(3840, 2160, 2.25)) - 1.55) < 1e-9);
  assert.equal(displayZoom(monitor(1920, 1200, 1.5)), 1);
  assert.deepEqual(scaledSize(SIZE, displayZoom(monitor(2560, 1440, 2.25))), { width: 744, height: 524, minWidth: 620, minHeight: 413 });
});

test('the Full HD boundary tolerates DIP rounding', () => {
  for (const height of [1079, 1080, 1081]) assert.equal(zoomFor(1.5, height), 1.25 / 1.5, height);
  for (const dips of [617, 618]) assert.equal(displayZoom({ scaleFactor: 1.75, bounds: { width: 1097, height: dips } }), 1.25 / 1.75);
  assert.equal(zoomFor(1.5, 1152), 1);
});

test('moving to another display scale keeps the layout size, resizing only once a drag ends', async () => {
  const { window, zooms, hooks, changeScale } = launcherWindow(1, { x: 400, y: 100, width: 1080, height: 760 });
  assert.deepEqual(zooms, []);
  hooks['did-navigate']();
  assert.deepEqual(zooms, [1]);
  await changeScale(1);
  assert.deepEqual(zooms, [1]);

  await changeScale(2.25);
  assert.deepEqual(zooms, [1, 1.55 / 2.25]);
  assert.deepEqual(window.min, [620, 413]);
  assert.deepEqual(window.bounds, { x: 568, y: 218, width: 744, height: 524 });

  hooks[WM_ENTERSIZEMOVE]();
  await changeScale(1);
  assert.deepEqual(zooms, [1, 1.55 / 2.25, 1]);
  assert.deepEqual(window.min, [900, 600]);
  assert.deepEqual(window.bounds, { x: 568, y: 218, width: 744, height: 524 });
  hooks[WM_EXITSIZEMOVE]();
  await tick();
  assert.deepEqual(window.bounds, { x: 400, y: 100, width: 1080, height: 761 });

  window.maximized = true;
  await changeScale(2.25);
  assert.equal(zooms.at(-1), 1.55 / 2.25);
  assert.deepEqual(window.bounds, { x: 400, y: 100, width: 1080, height: 761 });
  hooks['did-navigate']();
  assert.equal(zooms.at(-1), 1.55 / 2.25);
});

test('moving between a Full HD and a 1440p display follows the cap of each display', async () => {
  const { window, zooms, hooks, changeScale } = launcherWindow(1.5, { x: 200, y: 100, width: 900, height: 633 }, 1080);
  hooks['did-navigate']();
  assert.deepEqual(zooms, [1.25 / 1.5]);

  hooks[WM_ENTERSIZEMOVE]();
  await changeScale(2.25, 1440);
  assert.deepEqual(zooms, [1.25 / 1.5, 1.55 / 2.25]);
  assert.deepEqual(window.min, [620, 413]);
  assert.deepEqual(window.bounds, { x: 200, y: 100, width: 900, height: 633 });
  hooks[WM_EXITSIZEMOVE]();
  await tick();
  assert.deepEqual(window.bounds, { x: 278, y: 155, width: 744, height: 523 });

  await changeScale(1.5, 1080);
  assert.deepEqual(zooms, [1.25 / 1.5, 1.55 / 2.25, 1.25 / 1.5]);
  assert.deepEqual(window.min, [750, 500]);
  assert.deepEqual(window.bounds, { x: 200, y: 100, width: 900, height: 633 });
});

test('dragging to a display with the same scale but another cap refits once the drag ends', async () => {
  const { window, zooms, hooks, switchDisplay } = launcherWindow(1.5, { x: 200, y: 100, width: 900, height: 633 }, 1080);
  hooks[WM_ENTERSIZEMOVE]();
  switchDisplay(1.5, 1440);
  hooks[WM_EXITSIZEMOVE]();
  await tick();
  assert.deepEqual(zooms, [1]);
  assert.deepEqual(window.min, [900, 600]);
  assert.deepEqual(window.bounds, { x: 110, y: 37, width: 1080, height: 760 });
});

test('a scale change while maximized, minimized or fullscreen waits until the window is windowed again', async () => {
  for (const [state, leaving] of [['maximized', 'unmaximize'], ['minimized', 'restore'], ['fullScreen', 'leave-full-screen']]) {
    const { window, resizes, hooks, changeScale } = launcherWindow(1, { x: 400, y: 100, width: 1080, height: 760 });
    window[state] = true;
    await changeScale(2.25);
    assert.deepEqual(window.min, [620, 413], state);
    assert.deepEqual(window.bounds, { x: 400, y: 100, width: 1080, height: 760 }, state);
    assert.equal(resizes.length, 0, state);

    window[state] = false;
    hooks[leaving]();
    hooks[leaving]();
    await tick();
    assert.deepEqual(window.bounds, { x: 568, y: 218, width: 744, height: 524 }, state);
    assert.equal(resizes.length, 1, state);
  }
});

test('page zoom keys are stopped, other keys and AltGr characters pass through', () => {
  const { hooks } = launcherWindow(2.25, { x: 400, y: 100, width: 1080, height: 760 });
  const stopped = input => {
    let prevented = false;
    hooks['before-input-event']({ preventDefault: () => { prevented = true; } }, { type: 'keyDown', control: true, ...input });
    return prevented;
  };
  const zoomKeys = [{ key: '0', code: 'Digit0' }, { key: '-', code: 'Minus' }, { key: '=', code: 'Equal' },
    { key: '+', code: 'Equal', shift: true }, { key: '+', code: 'NumpadAdd' }, { key: '-', code: 'NumpadSubtract' },
    { key: '0', code: 'Numpad0' }];
  for (const input of zoomKeys) assert.equal(stopped(input), true, input.code);
  const passThrough = [{ key: 'c', code: 'KeyC' }, { key: 'v', code: 'KeyV' }, { key: 'x', code: 'KeyX' },
    { key: 'a', code: 'KeyA' }, { key: 'z', code: 'KeyZ' }, { key: '0', code: 'Digit0', control: false },
    { key: '0', code: 'Digit0', alt: true }, { type: 'keyUp', key: '0', code: 'Digit0' }];
  for (const input of passThrough) assert.equal(stopped(input), false, JSON.stringify(input));
});
