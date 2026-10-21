'use strict';

const fs = require('node:fs');
const path = require('node:path');

function repairGccSpecs(prefix) {
  // conda-unpack inserts a literal prefix into GCC's rpath spec. GCC parses
  // specs itself: shell quotes do not work, but backslash-escaped spaces do.
  const escaped = prefix.replace(/[\\\s]/g, '\\$&').replaceAll('%', '%%');
  if (escaped === prefix) return;
  const gcc = path.join(prefix, 'lib', 'gcc');
  if (!fs.existsSync(gcc)) return;
  for (const target of fs.readdirSync(gcc, { withFileTypes: true }).filter(item => item.isDirectory())) {
    const directory = path.join(gcc, target.name);
    for (const version of fs.readdirSync(directory, { withFileTypes: true }).filter(item => item.isDirectory())) {
      const versionDirectory = path.join(directory, version.name);
      for (const name of fs.readdirSync(versionDirectory).filter(name => name === 'specs' || /\.specs?$/.test(name))) {
        const file = path.join(versionDirectory, name);
        const original = fs.readFileSync(file, 'utf8');
        const repaired = original.replaceAll(`${prefix}/`, `${escaped}/`);
        if (repaired !== original) fs.writeFileSync(file, repaired);
      }
    }
  }
}

module.exports = { repairGccSpecs };
