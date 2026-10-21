'use strict';

const path = require('node:path');

// The Windows portable build keeps everything in this folder beside its .exe.
const FOLDER = 'SMS-Launcher-Portable';
const PATH_KEYS = ['repo', 'rom', 'installRoot', 'saveDirectory'];

// electron-builder's portable stub runs the app from a temporary copy and
// passes the folder the .exe was opened from.
function root(env = process.env, platform = process.platform) {
  const directory = env.PORTABLE_EXECUTABLE_DIR;
  return platform === 'win32' && directory ? path.join(directory, FOLDER) : null;
}

function inside(base, file) {
  const relative = path.relative(base, file);
  return relative === '' || (!path.isAbsolute(relative) && relative !== '..' && !relative.startsWith(`..${path.sep}`));
}

function mapPaths(saved, convert) {
  if (!saved || typeof saved !== 'object') return saved;
  const mapped = { ...saved };
  for (const key of PATH_KEYS) if (typeof mapped[key] === 'string' && mapped[key]) mapped[key] = convert(mapped[key]);
  if (mapped.previousInstall) mapped.previousInstall = mapPaths(mapped.previousInstall, convert);
  return mapped;
}

// Paths inside the portable folder are saved relative to it, so the folder
// still works after it moves to another drive or computer.
function storedPaths(config, base) {
  if (!base) return config;
  return mapPaths(config, value => path.isAbsolute(value) && inside(base, value) ? path.relative(base, value) || '.' : value);
}

function resolvedPaths(saved, base) {
  if (!base) return saved;
  return mapPaths(saved, value => path.isAbsolute(value) ? value : path.resolve(base, value));
}

// The game keeps its compiled shaders in %LOCALAPPDATA%\sms-port unless told
// otherwise; the portable build keeps them with everything else.
function shaderCache(base, arch) {
  return path.join(base, 'cache', `gx-programs-${arch === '32' ? '32' : '64'}.bin`);
}

// The same for the mip levels the game makes for HD textures (texture-mips beside the shaders).
function textureCache(base) { return path.join(base, 'cache', 'texture-mips'); }

module.exports = { FOLDER, root, storedPaths, resolvedPaths, shaderCache, textureCache };
