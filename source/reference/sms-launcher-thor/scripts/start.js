'use strict';
const { spawn } = require('node:child_process');
const electron = require('electron');
const args = process.platform === 'linux' ? ['--no-sandbox', '.'] : ['.'];
const child = spawn(electron, args, { stdio: 'inherit' });
child.on('error', error => { console.error(error); process.exitCode = 1; });
child.on('exit', code => { process.exitCode = code || 0; });
