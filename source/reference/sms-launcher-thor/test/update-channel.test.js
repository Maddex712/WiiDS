'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const { normalizeChannel, feedFor, channelLabel, channelsFromReleases } = require('../src/update-channel');

test('only stable, beta and pull request channels are accepted', () => {
  for (const value of ['stable', 'beta', 'pr-1', 'pr-123']) assert.equal(normalizeChannel(value), value);
  for (const value of [undefined, '', 'latest', 'pr-0', 'pr-', 'pr-1/../x', 'BETA', 'pr-12345678', 42])
    assert.equal(normalizeChannel(value), 'stable');
});

test('stable reads GitHub releases; previews read their own pre-release', () => {
  assert.deepEqual(feedFor('stable', 'me/app'), { provider: 'github', owner: 'me', repo: 'app' });
  assert.deepEqual(feedFor('pr-6', 'me/app'),
    { provider: 'generic', url: 'https://github.com/me/app/releases/download/pr-6' });
  assert.deepEqual(feedFor('beta', 'me/app'),
    { provider: 'generic', url: 'https://github.com/me/app/releases/download/beta' });
  assert.equal(feedFor('../evil', 'me/app').provider, 'github');
});

test('lists Stable, then Beta, then pull request previews newest first', () => {
  const channels = channelsFromReleases([
    { tag_name: 'v0.1.35', prerelease: false, name: 'v0.1.35' },
    { tag_name: 'pr-6', prerelease: true, name: 'Preview: Attach preview builds' },
    { tag_name: 'pr-12', prerelease: true, name: 'Preview: Another change' },
    { tag_name: 'beta', prerelease: true, name: 'Beta 0.1.36-beta.4' },
    { tag_name: 'pr-7', prerelease: true, draft: true, name: 'draft' },
    { tag_name: 'v0.1.36-rc', prerelease: true, name: 'other pre-release' }
  ]);
  assert.deepEqual(channels.map(item => item.id), ['stable', 'beta', 'pr-12', 'pr-6']);
  assert.equal(channels[2].label, 'Pull request #12');
  assert.equal(channelLabel('pr-6'), 'Pull request #6');
  assert.deepEqual(channelsFromReleases(null).map(item => item.id), ['stable']);
});
