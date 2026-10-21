'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { message, escape, main } = require('../scripts/discord-beta');

const game = { repository: 'https://github.com/chasem-dev/sms-pc-port.git', version: '2026.10.09-beta.0e104fa', commit: '0e104fa'.padEnd(40, '0') };
const base = { launcherVersion: '0.1.55-beta.27', launcherSha: 'c244bba'.padEnd(40, '0'), game,
  repository: 'chasem-dev/sms-launcher', now: new Date('2026-10-09T12:00:00Z') };

test('a Beta announcement names the launcher and game versions and links them', () => {
  const body = message({ ...base, beta: { launcher: ['Launcher change.'], game: ['Game *fix*.'] } });
  assert.deepEqual(body.allowed_mentions, { parse: [] }, 'notes can never ping anyone');
  const [embed] = body.embeds;
  assert.equal(embed.title, 'Beta 0.1.55-beta.27 is out');
  assert.equal(embed.url, 'https://github.com/chasem-dev/sms-launcher/releases/tag/beta');
  assert.deepEqual(embed.fields.slice(0, 2), [
    { name: 'Launcher', value: `[0.1.55-beta.27](https://github.com/chasem-dev/sms-launcher/commit/${base.launcherSha})`, inline: true },
    { name: 'Game', value: `[2026.10.09-beta.0e104fa](https://github.com/chasem-dev/sms-pc-port/commit/${game.commit})`, inline: true }
  ]);
  assert.equal(embed.fields[2].value, '• Launcher change.');
  assert.equal(embed.fields[3].value, '• Game \\*fix\\*.');
  assert.equal(embed.timestamp, '2026-10-09T12:00:00.000Z');
});

test('without Beta notes the announcement still has both versions', () => {
  for (const beta of [undefined, null, { launcher: [], game: 'not a list' }])
    assert.deepEqual(message({ ...base, beta }).embeds[0].fields.map(field => field.name), ['Launcher', 'Game']);
});

test('only notes the last Beta did not have are announced', () => {
  const beta = { launcher: ['Old launcher change.', 'New launcher change.'], game: ['Old game fix.', 'New game fix.'] };
  const previous = { launcher: ['Old launcher change.'], game: ['Old game fix.', 'A fix since released.'] };
  const fields = message({ ...base, beta, previous }).embeds[0].fields;
  assert.deepEqual(fields.slice(2).map(field => [field.name, field.value]),
    [['Launcher changes', '• New launcher change.'], ['Game changes', '• New game fix.']]);
});

test('a Beta with no new notes says so instead of repeating them', () => {
  const beta = { launcher: ['Launcher change.'], game: ['Game fix.'] };
  const fields = message({ ...base, beta, previous: beta }).embeds[0].fields;
  assert.deepEqual(fields.slice(2).map(field => field.name), ['Changes']);
  assert.match(fields[2].value, /^Nothing new in \[the changelog\]\(.*\) since the last Beta\.$/);
});

test('long notes are cut to fit Discord with a link to the changelog', () => {
  const lines = Array.from({ length: 30 }, (_, index) => `Change ${index} `.padEnd(100, 'x'));
  const value = message({ ...base, beta: { game: lines } }).embeds[0].fields[2].value;
  assert.ok(value.length <= 1024, `${value.length} characters`);
  assert.match(value, /…and \d+ more in \[the changelog\]\(https:\/\/github\.com\/chasem-dev\/sms-launcher\/blob\/main\/changelog\.json\)$/);
  const shown = value.split('\n').filter(line => line.startsWith('• ')).length;
  assert.equal(Number(value.match(/and (\d+) more/)[1]), lines.length - shown);
});

test('formatting characters in notes show as written', () => {
  assert.equal(escape('a_b *c* ~d~ `e` |f| > [g](h) \\'), 'a\\_b \\*c\\* \\~d\\~ \\`e\\` \\|f\\| \\> \\[g\\](h) \\\\');
});

test('the announcement posts to the webhook, and a missing secret or failed post never fails the build', async t => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'discord-beta-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  fs.mkdirSync(path.join(root, 'src'));
  fs.writeFileSync(path.join(root, 'src', 'game-release.json'), JSON.stringify({ ...game, version: '2026.10.09.1', commit: 'a'.repeat(40) }));
  fs.writeFileSync(path.join(root, 'changelog.json'), JSON.stringify({ beta: { launcher: ['Launcher change.'], game: [] } }));
  const posts = [], logs = [];
  t.mock.method(console, 'log', line => logs.push(line));
  t.mock.method(global, 'fetch', async (url, options) => { posts.push([url, JSON.parse(options.body)]); return { ok: posts.length === 1, status: 404, text: async () => 'Unknown Webhook' }; });
  const env = { GITHUB_REPOSITORY: base.repository, LAUNCHER_VERSION: base.launcherVersion, LAUNCHER_SHA: base.launcherSha,
    GAME_VERSION: game.version, GAME_COMMIT: game.commit };

  await main(env, root);
  assert.equal(posts.length, 0);
  assert.match(logs.pop(), /DISCORD_BETA_WEBHOOK is not set/);

  await main({ ...env, DISCORD_BETA_WEBHOOK: 'https://discord.test/api/webhooks/1/token' }, root);
  const [url, body] = posts[0];
  assert.equal(url, 'https://discord.test/api/webhooks/1/token');
  assert.match(body.embeds[0].fields[1].value, /^\[2026\.10\.09-beta\.0e104fa\]\(https:\/\/github\.com\/chasem-dev\/sms-pc-port\/commit\/0e104fa/,
    'the newest game Beta built, not the pin');
  assert.equal(body.embeds[0].fields[2].value, '• Launcher change.');
  assert.match(logs.pop(), /Told Discord about Beta 0\.1\.55-beta\.27 with game 2026\.10\.09-beta\.0e104fa/);

  fs.writeFileSync(path.join(root, 'previous.json'), JSON.stringify({ beta: { launcher: ['Launcher change.'], game: [] } }));
  await main({ ...env, DISCORD_BETA_WEBHOOK: 'https://discord.test/api/webhooks/1/token', PREVIOUS_CHANGELOG: path.join(root, 'previous.json') }, root);
  assert.deepEqual(posts[1][1].embeds[0].fields.slice(2).map(field => field.name), ['Changes'], 'notes the last Beta posted are not repeated');
  logs.pop();

  await main({ ...env, DISCORD_BETA_WEBHOOK: 'https://discord.test/api/webhooks/1/token', PREVIOUS_CHANGELOG: path.join(root, 'missing.json') }, root);
  assert.equal(posts[2][1].embeds[0].fields[2].value, '• Launcher change.', 'an unreadable last changelog announces every note');
  assert.match(logs.pop(), /^::warning::Could not post the Beta to Discord: HTTP 404: Unknown Webhook/);
});
