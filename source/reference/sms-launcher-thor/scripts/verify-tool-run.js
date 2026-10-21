'use strict';

// Publication retries reuse archives only after the original compile step passed.
const { execFileSync } = require('node:child_process');
const repository = 'chasem-dev/sms-launcher';
const runId = process.env.SMS_VERIFIED_RUN;
if (!/^\d+$/.test(runId || '')) throw new Error('A previous workflow run ID is required.');
const api = resource => JSON.parse(execFileSync('gh', ['api', `repos/${repository}/${resource}`], { encoding: 'utf8' }));
const run = api(`actions/runs/${runId}`);
const workflow = api('actions/workflows/build-tool-assets.yml');
if (run.workflow_id !== workflow.id || run.head_repository.full_name !== repository)
  throw new Error('The archives must come from this repository’s build tool workflow.');
const { jobs } = api(`actions/runs/${runId}/jobs?per_page=100`);
const selected = process.env.SMS_VERIFIED_PLATFORM;
const platforms = selected === 'all' ? ['linux', 'win32'] : [selected];
for (const platform of platforms) {
  if (!['linux', 'win32'].includes(platform)) throw new Error('Unsupported platform.');
  const job = jobs.find(item => item.name.endsWith(`, ${platform})`));
  if (job?.conclusion !== 'success' || !job.steps.some(step =>
    step.name === 'Compile with the relocated archive' && step.conclusion === 'success') ||
      !job.steps.some(step => step.name === 'Compile and start Eclipse with the relocated archive' && step.conclusion === 'success'))
    throw new Error(`The previous ${platform} compilation check did not succeed.`);
  process.stdout.write(`Verified ${platform} tools in workflow ${runId}, source ${run.head_sha}.\n`);
}
