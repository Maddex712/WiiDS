'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const prompts = require('../src/prompts');

test('unknown prompt styles fall back to the GameCube glyphs', () => {
  assert.equal(prompts.normalizeStyle(undefined), 'gamecube');
  assert.equal(prompts.normalizeStyle('nes'), 'gamecube');
  assert.equal(prompts.normalizeStyle('playstation'), 'playstation');
});

test('the original prompts are the default, and a choice for controllers under Automatic', () => {
  assert.equal(prompts.STYLES[0].id, 'gamecube');
  assert.equal(prompts.normalizePad('gamecube'), 'gamecube');
  assert.equal(prompts.normalizePad(undefined), 'match');
  assert.equal(prompts.normalizePad('playstation'), 'playstation');
});

test('controller prompts follow the default layout in each style', () => {
  assert.deepEqual(prompts.icon('xbox', 'A', {}, {}), { kind: 'face', text: 'A', fill: '#107c10' });
  assert.equal(prompts.icon('playstation', 'A', {}, {}).symbol, 'cross');
  assert.equal(prompts.icon('playstation', 'Y', {}, {}).symbol, 'triangle');
  assert.deepEqual(prompts.icon('xbox', 'Z', {}, {}), { kind: 'shoulder', text: 'RB' });
  assert.deepEqual(prompts.icon('playstation', 'L', {}, {}), { kind: 'trigger', text: 'L2' });
  assert.deepEqual(prompts.icon('steamdeck', 'R', {}, {}), { kind: 'trigger', text: 'R2' });
  assert.equal(prompts.icon('xbox', 'CSTICK', {}, {}).kind, 'stick');
});

test('remapped controller buttons show as bound', () => {
  assert.equal(prompts.icon('playstation', 'A', {}, { A: ['PAD_B'] }).symbol, 'circle');
  assert.deepEqual(prompts.icon('xbox', 'Z', {}, { Z: ['PAD_LB', 'PAD_RB'] }), { kind: 'shoulder', text: 'LB' });
  assert.deepEqual(prompts.icon('xbox', 'B', {}, { B: ['PAD_DPLEFT'] }), { kind: 'dpad', direction: 'left' });
});

test('keyboard prompts show the first bound key, remapped or not', () => {
  assert.deepEqual(prompts.icon('keyboard', 'A', {}, {}), { kind: 'key', text: 'Space' });
  assert.deepEqual(prompts.icon('keyboard', 'L', {}, {}), { kind: 'key', text: 'Q' });
  assert.deepEqual(prompts.icon('keyboard', 'A', { A: ['KP_0'] }, {}), { kind: 'key', text: 'Num0' });
  assert.deepEqual(prompts.icon('keyboard', 'Z', { Z: [] }, {}), { kind: 'none', text: 'Z' });
});

test('the keyboard C-stick prompt is the mouse with mouse look, else the camera keys', () => {
  assert.deepEqual(prompts.icon('keyboard', 'CSTICK', {}, {}, { mouseCamera: true }), { kind: 'mouse' });
  assert.deepEqual(prompts.icon('keyboard', 'CSTICK', {}, {}), { kind: 'key', text: 'IJKL' });
});

test('every style draws eight icons, in the game\'s glyph order', () => {
  assert.deepEqual(prompts.GLYPHS, ['A', 'B', 'X', 'Y', 'Z', 'L', 'R', 'CSTICK']);
  for (const style of prompts.DRAWN) assert.equal(prompts.icons(style, {}, {}).length, 8);
});
