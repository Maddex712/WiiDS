'use strict';

const FULL_HD_HEIGHT = 1080, FULL_HD_SCALE_LIMIT = 1.25, SCALE_LIMIT = 1.55;
const WM_DPICHANGED = 0x02E0, WM_ENTERSIZEMOVE = 0x0231, WM_EXITSIZEMOVE = 0x0232;
const ZOOM_KEYS = new Set(['0', '-', '=', '+']);
const ZOOM_CODES = new Set(['Digit0', 'Minus', 'Equal', 'Numpad0', 'NumpadSubtract', 'NumpadAdd']);

function physicalHeight({ bounds, scaleFactor }) {
  return Math.min(bounds.width, bounds.height) * scaleFactor;
}

// DIP bounds are whole numbers, so the pixel height is only known to within one DIP.
function scaleLimit(scaleFactor, pixelHeight) {
  return pixelHeight <= FULL_HD_HEIGHT + scaleFactor ? FULL_HD_SCALE_LIMIT : SCALE_LIMIT;
}

// Above the limit for its display, Windows display scaling zooms the launcher out to look as it does at the limit.
function zoomFor(scaleFactor, pixelHeight) {
  const limit = scaleLimit(scaleFactor, pixelHeight);
  return scaleFactor > limit ? limit / scaleFactor : 1;
}

function displayZoom(display) {
  return zoomFor(display.scaleFactor, physicalHeight(display));
}

// AltGr arrives as Ctrl+Alt and types characters on some layouts.
function isManualZoomKey(input) {
  return input.type === 'keyDown' && input.control && !input.alt && (ZOOM_KEYS.has(input.key) || ZOOM_CODES.has(input.code));
}

function scaledSize(size, zoom) {
  return Object.fromEntries(Object.entries(size).map(([key, value]) => [key, Math.round(value * zoom)]));
}

function resizedBounds(bounds, ratio, area) {
  const width = Math.round(bounds.width * ratio), height = Math.round(bounds.height * ratio);
  const x = Math.round(bounds.x + (bounds.width - width) / 2), y = Math.round(bounds.y + (bounds.height - height) / 2);
  return { width, height,
    x: Math.max(area.x, Math.min(x, area.x + area.width - width)),
    y: Math.max(area.y, Math.min(y, area.y + area.height - height)) };
}

// Resizing waits for a drag to end: mid-drag it could pull the window back onto the display it came from.
function followDisplayScale(window, screen, size, zoom) {
  let sizedFor = zoom, dragging = false;
  const isWindowed = () => !window.isMaximized() && !window.isFullScreen() && !window.isMinimized();
  const resize = () => {
    if (dragging || sizedFor === zoom || window.isDestroyed() || !isWindowed()) return;
    const bounds = window.getBounds();
    window.setBounds(resizedBounds(bounds, zoom / sizedFor, screen.getDisplayMatching(bounds).workArea));
    sizedFor = zoom;
  };
  // Zoom only applies to a loaded page, and Chromium restores the zoom from the last run on each load.
  const applyZoom = () => window.webContents.setZoomFactor(zoom);
  const fit = () => {
    if (window.isDestroyed()) return;
    const next = displayZoom(screen.getDisplayMatching(window.getBounds()));
    if (next === zoom) return;
    zoom = next;
    applyZoom();
    const { minWidth, minHeight } = scaledSize(size, zoom);
    window.setMinimumSize(minWidth, minHeight);
    resize();
  };
  window.webContents.on('did-navigate', applyZoom);
  // preventDefault here also stops the default menu's zoom accelerators.
  window.webContents.on('before-input-event', (event, input) => { if (isManualZoomKey(input)) event.preventDefault(); });
  window.hookWindowMessage(WM_DPICHANGED, () => setImmediate(fit));
  window.hookWindowMessage(WM_ENTERSIZEMOVE, () => { dragging = true; });
  // Displays with the same scale but a different cap send no WM_DPICHANGED.
  window.hookWindowMessage(WM_EXITSIZEMOVE, () => { dragging = false; setImmediate(() => { fit(); resize(); }); });
  // leave-full-screen fires before the window reports that it has left fullscreen.
  for (const event of ['restore', 'unmaximize', 'leave-full-screen']) window.on(event, () => setImmediate(resize));
  const displayChanged = (_event, _display, changed) => { if (changed.includes('scaleFactor') || changed.includes('bounds')) fit(); };
  screen.on('display-metrics-changed', displayChanged);
  window.once('closed', () => screen.off('display-metrics-changed', displayChanged));
  fit();
}

module.exports = { zoomFor, displayZoom, scaledSize, followDisplayScale };
