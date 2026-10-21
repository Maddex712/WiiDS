'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const presets = require('../src/presets');
const port = require('../src/port');

test('every preset sets every picture setting to a value the launcher accepts', () => {
  for (const preset of presets.PRESETS) {
    for (const key of presets.KEYS) assert.ok(key in preset.values, `${preset.id} sets ${key}`);
    const normalized = port.normalizeSettings(presets.apply({}, preset.id), 'win32');
    for (const [key, value] of Object.entries(preset.values)) assert.equal(normalized[key], value, `${preset.id} ${key}`);
    assert.equal(normalized.graphicsPreset, preset.id);
  }
});

test('the presets climb from Low to Ultra', () => {
  const [low, medium, high, ultra] = ['low', 'medium', 'high', 'ultra'].map(id => presets.find(id).values);
  assert.deepEqual([low, medium, high, ultra].map(values => values.resolution), [1, 2, 3, 4]);
  assert.deepEqual([low, medium, high, ultra].map(values => values.msaa), [0, 0, 4, 8]);
  assert.deepEqual([low, medium, high, ultra].map(values => values.anisotropic), [0, 4, 8, 16]);
});

test('the Steam Deck preset suits its 16:10 screen, full screen', () => {
  const deck = port.normalizeSettings(presets.apply({ widescreen: '21:9', fullscreen: false }, 'steamdeck'), 'linux');
  assert.equal(deck.widescreen, '16:10');
  assert.equal(deck.fullscreen, true);
  assert.equal(deck.resolution, 2);
  assert.equal(deck.graphicsPreset, 'steamdeck');
});

test('changing a picture setting by hand makes the preset Custom', () => {
  const high = presets.apply(port.normalizeSettings({}, 'win32'), 'high');
  assert.equal(port.normalizeSettings(high, 'win32').graphicsPreset, 'high');
  assert.equal(port.normalizeSettings({ ...high, msaa: 2 }, 'win32').graphicsPreset, 'custom');
  assert.equal(port.normalizeSettings({ ...high, heatHaze: false }, 'win32').graphicsPreset, 'custom');
  // settings the presets leave alone keep the preset
  assert.equal(port.normalizeSettings({ ...high, brightness: 120, frameRate: 120 }, 'win32').graphicsPreset, 'high');
  const deck = presets.apply(port.normalizeSettings({}, 'linux'), 'steamdeck');
  assert.equal(port.normalizeSettings({ ...deck, fullscreen: false }, 'linux').graphicsPreset, 'custom');
});

test('unknown or missing presets are Custom, and Custom changes nothing', () => {
  assert.equal(port.normalizeSettings({}, 'win32').graphicsPreset, 'custom');
  assert.equal(port.normalizeSettings({ graphicsPreset: 'potato' }, 'win32').graphicsPreset, 'custom');
  const settings = { resolution: 3, msaa: 2 };
  assert.deepEqual(presets.apply(settings, 'custom'), { ...settings, graphicsPreset: 'custom' });
});
