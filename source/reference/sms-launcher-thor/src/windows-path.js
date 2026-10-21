'use strict';

// Windows spells it Path. process.env finds it under any spelling, but a copy
// ({ ...process.env }) keeps only Path, and a child given both PATH and Path
// receives only PATH. Setting PATH from a copy's env.PATH therefore drops
// System32, so cmd.exe can't run its own commands (e.g. a `mode` in AutoRun).
function windowsPath(env) {
  if (typeof env.PATH === 'string') return env.PATH;
  const key = Object.keys(env).find(name => name.toUpperCase() === 'PATH');
  return key ? env[key] : '';
}

// A copy of env with one PATH: entries first, then the existing path under any spelling.
function prependWindowsPath(env, entries) {
  const result = Object.fromEntries(Object.entries(env).filter(([name]) => name.toUpperCase() !== 'PATH'));
  result.PATH = [...entries, windowsPath(env)].filter(Boolean).join(';');
  return result;
}

module.exports = { windowsPath, prependWindowsPath };
