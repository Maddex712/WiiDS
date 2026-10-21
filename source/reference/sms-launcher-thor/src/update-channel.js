'use strict';

// Launcher update channels. Stable follows GitHub's latest release, which never
// includes a pre-release. Beta and each pull request's preview are moving
// pre-releases (tags "beta" and "pr-<number>") read as plain update feeds.
const REPOSITORY = 'chasem-dev/sms-launcher';
const CHANNEL = /^(?:stable|beta|pr-[1-9]\d{0,6})$/;

function normalizeChannel(value) {
  return typeof value === 'string' && CHANNEL.test(value) ? value : 'stable';
}

function feedFor(channel, repository = REPOSITORY) {
  const [owner, repo] = repository.split('/');
  channel = normalizeChannel(channel);
  return channel === 'stable' ? { provider: 'github', owner, repo }
    : { provider: 'generic', url: `https://github.com/${repository}/releases/download/${channel}` };
}

function channelLabel(channel) {
  channel = normalizeChannel(channel);
  if (channel === 'stable') return 'Stable';
  if (channel === 'beta') return 'Beta';
  return `Pull request #${channel.slice(3)}`;
}

// GitHub's release list -> the channels a player can choose, Stable first.
function channelsFromReleases(releases) {
  const channels = [{ id: 'stable', label: 'Stable', detail: 'Released versions (recommended)' }];
  const previews = [];
  for (const release of Array.isArray(releases) ? releases : []) {
    if (!release || release.draft || !release.prerelease || !CHANNEL.test(release.tag_name) || release.tag_name === 'stable') continue;
    const entry = { id: release.tag_name, label: channelLabel(release.tag_name),
      detail: String(release.name || '').slice(0, 120), publishedAt: release.published_at || null };
    if (release.tag_name === 'beta') channels.push(entry);
    else previews.push(entry);
  }
  previews.sort((a, b) => Number(b.id.slice(3)) - Number(a.id.slice(3)));
  return channels.concat(previews);
}

module.exports = { REPOSITORY, normalizeChannel, feedFor, channelLabel, channelsFromReleases };
