'use strict';
const fs = require('node:fs');
const path = require('node:path');
const { formatEntry } = require('./activity-log');

// The activity view is bounded; keep the complete session on disk so earlier
// diagnostics cannot disappear behind verbose builds or a final stub report.
function createSessionLog(directory, onError) {
  let file = null, failed = false;
  function ensureFile() {
    if (!file) {
      const folder = path.join(directory(), 'logs');
      fs.mkdirSync(folder, { recursive: true });
      file = path.join(folder, `activity-${new Date().toISOString().replace(/[:.]/g, '-')}-${process.pid}.log`);
      fs.writeFileSync(file, '', { mode: 0o600 });
    }
    return file;
  }
  return {
    reset() {
      failed = false;
      try { fs.writeFileSync(ensureFile(), ''); }
      catch (error) { failed = true; onError(error); }
    },
    write(text, stream) {
      if (failed) return;
      try { fs.appendFileSync(ensureFile(), `${formatEntry({ text, stream })}\n`); }
      catch (error) { failed = true; onError(error); }
    },
    read(partial = []) {
      if (failed) throw new Error('The session log could not be copied. See the activity log.');
      return fs.readFileSync(ensureFile(), 'utf8') +
        (partial.length ? partial.map(formatEntry).join('\n') + '\n' : '');
    },
    save(destination, partial = []) {
      if (failed) throw new Error('The session log could not be saved. See the activity log.');
      fs.copyFileSync(ensureFile(), destination);
      if (partial.length) fs.appendFileSync(destination, partial.map(formatEntry).join('\n') + '\n');
    }
  };
}
module.exports = { createSessionLog };
