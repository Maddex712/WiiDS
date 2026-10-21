#!/usr/bin/env node
'use strict';

// Shows the launcher's Discord Rich Presence on the Discord app running on
// this computer, without the launcher.
//
//   node scripts/presence-demo.js                    a scripted tour of the game's screens
//   node scripts/presence-demo.js --game SMS DISC    play: the game's own [presence] lines
//
// SMS is a game build with platform/presence (sms-pc-port's build/<os>-<arch>/sms),
// DISC your GMSE01 disc image. Ctrl+C clears the activity.
const fs = require('node:fs');
const { spawn } = require('node:child_process');
const discord = require('../src/discord');
const presence = require('../src/presence');

const STEP = 12000;
const startedAt = Date.now();
const playtime = 42 * 3600 * 1000;

// TApplication states, as the game reports them.
const GAMEPLAY = 5, TITLE = 8, BOOT = 2;
const base = { state: GAMEPLAY, area: 1, episode: 0, shines: 34, blueCoins: 12, lives: 5, saveFile: 0, paused: 0, cutscene: 0 };
const TOUR = [
  ['Starting up', { state: BOOT, shines: 0, blueCoins: 0 }],
  ['Title screen', { area: 15 }],
  ['Delfino Plaza', { area: 1 }],
  ['Episode select', { state: TITLE, area: 2 }],
  ['Bianco Hills, episode 2', { area: 2, episode: 1 }],
  ['Paused', { area: 2, episode: 1, paused: 1 }],
  ['Cutscene', { area: 2, episode: 1, cutscene: 1 }],
  ['Shine collected', { area: 2, episode: 1, shines: 35 }],
  ['Secret course', { area: 0x1e, shines: 35 }],
  ['Hotel Delfino', { area: 7, episode: 2, shines: 35 }],
  ['Boss fight', { area: 0x3a, shines: 35 }],
  ['Corona Mountain', { area: 0x34, shines: 120, blueCoins: 240 }]
];

const client = discord.createPresence({ log: message => console.log(`discord: ${message}`) });
function show(game, label) {
  const activity = presence.activity(game, { startedAt, playtime: playtime + Date.now() - startedAt });
  client.set(activity);
  console.log(`${label ? `${label}: ` : ''}${activity.details || 'Super Mario Sunshine'}${activity.state ? ` | ${activity.state}` : ''}`
    + ` | ${activity.assets.large_image}`);
}

function stop(code = 0) {
  client.stop();
  console.log('Cleared the activity.');
  process.exit(code);
}
process.on('SIGINT', () => stop());

const found = discord.socketPaths().filter(target => process.platform === 'win32' || fs.existsSync(target));
console.log(found.length ? `Discord found: ${found[0]}` : 'Discord is not running on this computer; open it and this keeps looking.');
console.log('Your own profile shows the activity; the buttons only show to others.\n');

const args = process.argv.slice(2);
if (args[0] === '--game') {
  const [binary, disc] = args.slice(1);
  if (!binary || !disc) { console.error('usage: presence-demo.js --game SMS DISC'); process.exit(2); }
  show(null, 'Game starting');
  const game = spawn(binary, [disc], { env: { ...process.env, SMS_PRESENCE: '1' }, stdio: ['ignore', 'pipe', 'inherit'] });
  let pending = '';
  game.stdout.on('data', chunk => {
    const lines = (pending + chunk).split('\n');
    pending = lines.pop();
    for (const line of lines) {
      const state = presence.parseLine(line.trim());
      if (state) show(state);
      else if (line.trim()) console.log(`game: ${line.trim()}`);
    }
  });
  game.on('close', code => { console.log(`Game exited (${code}).`); stop(); });
} else {
  let step = 0;
  const next = () => {
    const [label, fields] = TOUR[step++ % TOUR.length];
    show({ ...base, ...fields }, label);
  };
  next();
  setInterval(next, STEP);
}
