'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

function verify(eclipseConclusion) {
  const fixture = {
    run: { workflow_id: 123, head_repository: { full_name: 'chasem-dev/sms-launcher' }, head_sha: 'tested-source' },
    workflow: { id: 123 },
    jobs: { jobs: [{
      name: 'prepare-tools (windows-2025, win32)', conclusion: 'success',
      steps: [
        { name: 'Compile with the relocated archive', conclusion: 'success' },
        { name: 'Compile and start Eclipse with the relocated archive', conclusion: eclipseConclusion }
      ]
    }] }
  };
  return spawnSync(process.execPath, ['-e', `
    const fixture = JSON.parse(process.env.SMS_TEST_TOOL_RUN);
    require('node:child_process').execFileSync = (command, args) => {
      if (command !== 'gh') throw new Error('Unexpected command');
      const resource = args.at(-1);
      return JSON.stringify(resource.endsWith('/jobs?per_page=100') ? fixture.jobs
        : resource.endsWith('/build-tool-assets.yml') ? fixture.workflow : fixture.run);
    };
    require(process.argv[1]);
  `, path.join(__dirname, '..', 'scripts', 'verify-tool-run.js')], {
    encoding: 'utf8', env: { ...process.env, SMS_TEST_TOOL_RUN: JSON.stringify(fixture),
      SMS_VERIFIED_RUN: '456', SMS_VERIFIED_PLATFORM: 'win32' }
  });
}

test('publication retry accepts tools whose standard and Eclipse builds passed', () => {
  const result = verify('success');
  assert.equal(result.status, 0, result.stderr);
  assert.match(result.stdout, /Verified win32 tools/);
});

test('publication retry refuses failed or skipped Eclipse checks even when the job reports success', () => {
  for (const conclusion of ['failure', 'skipped', undefined]) {
    const result = verify(conclusion);
    assert.equal(result.status, 1);
    assert.match(result.stderr, /previous win32 compilation check did not succeed/);
  }
});
