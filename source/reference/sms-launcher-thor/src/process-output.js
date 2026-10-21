'use strict';

const { StringDecoder } = require('node:string_decoder');
const { cleanOutputLine, failureReason } = require('./progress');

const WINDOWS_STATUSES = new Map([
  [0xC0000005, 'access violation'],
  [0xC0000017, 'out of memory'],
  [0xC000001D, 'illegal instruction'],
  [0xC0000094, 'integer division by zero'],
  [0xC00000FD, 'stack overflow'],
  [0xC0000135, 'DLL not found'],
  [0xC0000139, 'DLL entry point not found'],
  [0xC0000142, 'DLL initialization failed'],
  [0xC0000374, 'heap corruption'],
  [0xC0000409, 'fast-fail'],
  [0xC0000602, 'fail-fast exception']
]);

// Each pipe keeps its own decoder and partial line. Show partial output as it
// arrives, then update that entry instead of duplicating it at the next chunk.
function observeProcess(child, { output, line = () => {}, capture = false }) {
  return new Promise(resolve => {
    const readers = [];
    const captured = { stdout: '', stderr: '' };
    let error = null;
    for (const stream of ['stdout', 'stderr']) {
      const decoder = new StringDecoder('utf8');
      let pending = '', entry = null, afterCR = false;
      function publish() {
        const text = cleanOutputLine(pending);
        if (!entry || entry.text !== text) entry = output(text, stream, entry);
      }
      function consume(text) {
        if (capture) captured[stream] += text;
        let start = 0;
        for (let i = 0; i < text.length; ++i) {
          const character = text[i];
          if (afterCR && character === '\n') { afterCR = false; start = i + 1; continue; }
          afterCR = false;
          if (character !== '\r' && character !== '\n') continue;
          pending += text.slice(start, i);
          publish();
          line(cleanOutputLine(pending), stream, entry);
          pending = ''; entry = null;
          afterCR = character === '\r';
          start = i + 1;
        }
        pending += text.slice(start);
        if (pending) publish();
      }
      child[stream].on('data', bytes => consume(decoder.write(bytes)));
      readers.push(() => {
        consume(decoder.end());
        if (pending) line(cleanOutputLine(pending), stream, entry);
      });
    }
    child.on('error', value => { error = value; });
    child.on('close', (code, signal) => {
      for (const end of readers) end();
      resolve({ code, signal, error, ...captured });
    });
  });
}

function exitDescription(label, code, signal, platform = process.platform) {
  if (signal) return `${label} terminated by ${signal}`;
  const status = Number.isInteger(code) ? code >>> 0 : 0;
  const native = platform === 'win32' && status >= 0x80000000;
  const detail = native ? ` (0x${status.toString(16).toUpperCase().padStart(8, '0')}${WINDOWS_STATUSES.has(status) ? `: ${WINDOWS_STATUSES.get(status)}` : ''})` : '';
  return `${label} exited with code ${code}${detail}`;
}

function failureMessage(label, result, recent, { game = false, platform = process.platform } = {}) {
  const native = platform === 'win32' && Number.isInteger(result.code) && (result.code >>> 0) >= 0x80000000;
  // Game reports contain ordinary warnings and stage state. They do not tell
  // us what caused an abrupt exit, so quote only the actual process status.
  const reason = game || result.signal || native ? '' : failureReason(recent);
  return `${exitDescription(label, result.code, result.signal, platform)}${reason ? `: ${reason}` : ''}. See the activity log.`;
}

module.exports = { observeProcess, exitDescription, failureMessage };
