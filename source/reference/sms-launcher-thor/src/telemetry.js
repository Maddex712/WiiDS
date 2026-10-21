'use strict';

// Anonymous usage heartbeats to sms-server-api, and the online count shown in
// the top bar. A heartbeat carries only the launcher and game versions, OS,
// update channel, whether the game is running and a random install ID.
// Players who turn sharing off still see the count, but are not part of it.
// SMS_TELEMETRY_URL overrides it for testing.
const TELEMETRY_URL = 'https://sms-launcher-telemetry.chasemdev.workers.dev';
const INTERVAL = 5 * 60 * 1000;
const INSTALL_ID = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i;

function endpoint(env = process.env) {
  return (env.SMS_TELEMETRY_URL || TELEMETRY_URL).replace(/\/+$/, '');
}

function validInstallId(value) { return typeof value === 'string' && INSTALL_ID.test(value); }

function payload({ installId, launcherVersion, gameVersion, availableVersion, channel, playing },
  platform = process.platform, arch = process.arch) {
  return { installId, launcherVersion, gameVersion: gameVersion || null, availableVersion: availableVersion || null,
    platform, arch, channel, playing: Boolean(playing) };
}

function onlineCount(body) {
  const count = value => Number.isInteger(value) && value >= 0;
  return body && count(body.online) ? { online: body.online, playing: count(body.playing) ? body.playing : 0 } : null;
}

// Each tick sends a heartbeat when sharing is on, or only asks for the count
// when it is off. Offline or failed requests keep the last count.
function createReporter({ url, fetch, sharing, getPayload, onCount, intervalMs = INTERVAL,
  setInterval = global.setInterval, clearInterval = global.clearInterval }) {
  let timer = null;
  async function tick() {
    try {
      const response = sharing()
        ? await fetch(`${url}/v1/heartbeat`, { method: 'POST', headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(getPayload()) })
        : await fetch(`${url}/v1/online`);
      if (!response.ok) return;
      const count = onlineCount(await response.json());
      if (count) onCount(count);
    } catch (_) { /* offline: the count shown stays as it was */ }
  }
  return {
    tick,
    start() {
      if (!timer) timer = setInterval(tick, intervalMs);
      return tick();
    },
    stop() {
      if (timer) clearInterval(timer);
      timer = null;
    }
  };
}

module.exports = { TELEMETRY_URL, INTERVAL, endpoint, validInstallId, payload, onlineCount, createReporter };
