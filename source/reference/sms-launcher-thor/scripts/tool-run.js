'use strict';

const { spawn } = require('node:child_process');

function run(command, args, options = {}, label = command) {
  process.stdout.write(`\n${label}\n`);
  return new Promise((resolve, reject) => {
    const child = spawn(command, args, { ...options, stdio: 'inherit', windowsHide: true });
    child.on('error', reject);
    child.on('close', code => code === 0 ? resolve() : reject(new Error(`${label} exited with ${code}`)));
  });
}

function runMain(main) {
  let finished = false;
  process.once('beforeExit', () => {
    if (!finished) {
      process.stderr.write('The tool operation did not finish.\n');
      process.exitCode = 1;
    }
  });
  Promise.resolve().then(main).then(() => { finished = true; }, error => {
    finished = true;
    process.stderr.write(`${error.stack}\n${error.path || ''}\n`);
    process.exitCode = 1;
  });
}

module.exports = { run, runMain };
