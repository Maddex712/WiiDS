'use strict';

const { StringDecoder } = require('node:string_decoder');

function cleanOutputLine(line) {
  return String(line).replace(/\x1b\][^\x07\x1b]*(?:\x07|\x1b\\)/g, '').replace(/\x1b\[[0-?]*[ -/]*[@-~]/g, '');
}

// Pipe output can split a UTF-8 character, a progress line, or a CRLF across
// chunks. Each stream gets its own reader, including its final unterminated line.
function createLineReader(onLine) {
  const decoder = new StringDecoder('utf8');
  let pending = '';
  function consume(text) {
    const parts = (pending + text).split(/[\r\n]/);
    pending = parts.pop();
    for (const part of parts) if (part) onLine(cleanOutputLine(part));
  }
  return {
    write(bytes) { consume(decoder.write(bytes)); },
    end() { consume(decoder.end()); if (pending) onLine(cleanOutputLine(pending)); pending = ''; }
  };
}

function buildDetail(output) {
  return /^(?:Linking|Generating .*syms|Renaming)/i.test(output) ? 'Finishing the game build' : 'Building your game';
}

function activityFromLine(line) {
  const value = cleanOutputLine(line).trim();
  if (!value) return null;
  const output = value.replace(/^\[\s*(?:\d{1,3}%|\d+\s*\/\s*\d+)\]\s*/, '');
  // These run after compilation. Their command may print 100% before copying
  // the disc, so keep this separate phase indeterminate until the process exits.
  if (/^(?:Bundling|Extracting (?:the )?(?:SMS\.app|sms\.exe) icon|bundle_disc:)/.test(output))
    return { detail: 'Preparing your game files', percent: null };
  if (/^Built .*[\/]sms(?:\.exe)?\s/.test(value) || /^ninja: no work to do\./.test(value))
    return { detail: 'Finishing setup', percent: null };
  const ninja = value.match(/^\[\s*(\d+)\s*\/\s*(\d+)\]\s*(.*)/);
  if (ninja) {
    const completed = Number(ninja[1]), total = Number(ninja[2]);
    if (!Number.isSafeInteger(completed) || !Number.isSafeInteger(total) || total <= 0 || completed > total) return null;
    return { detail: `${buildDetail(ninja[3])} · ${completed.toLocaleString('en-US')} of ${total.toLocaleString('en-US')} steps`,
      percent: Math.floor(completed * 100 / total) };
  }
  const build = value.match(/^\[\s*(\d{1,3})%\]\s*(.*)/);
  if (build) return { detail: buildDetail(build[2]), percent: Math.min(100, Number(build[1])) };
  const git = value.match(/^(Receiving objects|Resolving deltas|Updating files|Compressing objects):\s*(\d{1,3})%/);
  if (git) return { detail: git[1] === 'Receiving objects' ? 'Downloading files' : 'Preparing downloaded files', percent: Math.min(100, Number(git[2])) };
  if (/^Downloading Super Mario Sunshine UHD Texture Pack/.test(value))
    return { detail: 'Downloading HD textures (about 1 GB)', percent: null };
  if (/^Downloading Super Mario Sunshine HD texture extras/.test(value))
    return { detail: 'Downloading HD texture extras', percent: null };
  const textureDownload = value.match(/^Texture download: (\d+)\/(\d+) bytes$/);
  if (textureDownload) {
    const received = Number(textureDownload[1]), total = Number(textureDownload[2]);
    if (!Number.isSafeInteger(received) || !Number.isSafeInteger(total) || (total && received > total)) return null;
    const mb = bytes => `${(bytes / 1e6).toLocaleString('en-US', { maximumFractionDigits: 1 })} MB`;
    return { detail: `Downloading HD textures · ${mb(received)}${total ? ` of ${mb(total)}` : ' downloaded'}`,
      percent: total ? Math.floor(received / total * 100) : null };
  }
  if (value === 'Checking HD cutscenes') return { detail: 'Checking HD cutscenes', percent: null };
  if (value === 'Preparing HD cutscenes from your disc') return { detail: 'Preparing HD cutscenes from your disc', percent: null };
  const movieDownload = value.match(/^HD movie download: (\d+)\/(\d+) bytes(?: \(movie (\d+)\/21\))?$/);
  if (movieDownload) {
    const done = Number(movieDownload[1]), total = Number(movieDownload[2]);
    if (!Number.isSafeInteger(done) || !Number.isSafeInteger(total) || total <= 0 || done > total) return null;
    const movie = Number(movieDownload[3]);
    if (movieDownload[3] && (movie < 1 || movie > 21)) return null;
    return { detail: `Downloading HD cutscenes${movie ? ` · movie ${movie} of 21` : ''} · ${(done / 1e6).toFixed(1)} of ${(total / 1e6).toFixed(1)} MB`,
      percent: Math.floor(movie ? (movie - 1 + done / total) * 100 / 21 : done * 100 / total) };
  }
  const movies = value.match(/^Installing HD cutscenes: (\d+)\/21 movies$/);
  if (movies && Number(movies[1]) <= 21) return { detail: `Installing HD cutscenes · ${movies[1]} of 21 movies`,
    percent: Math.floor(Number(movies[1]) * 100 / 21) };
  if (value === 'HD cutscenes installed: 21/21 movies') return { detail: 'HD cutscenes installed', percent: 100 };
  if (value === 'Checking HD texture download')
    return { detail: 'Checking HD textures', percent: null };
  if (/^Unpacking GMS\.7z/.test(value))
    return { detail: 'Installing HD textures', percent: null };
  if (/^Installed \d+ textures/.test(value))
    return { detail: 'HD textures installed', percent: 100 };
  if (/^Removing the previous install/.test(value))
    return { detail: 'Making room for HD textures', percent: null };
  if (/^\s*\d+ MiB, MD5 /.test(value))
    return { detail: 'Download checked', percent: null };
  // cmake/patches.cmake: one process per decomp patch, slow on some computers.
  const patching = value.match(/^-- SMS port: applying decomp patches \((\d+)\/(\d+)\)$/);
  if (patching) {
    const done = Number(patching[1]), total = Number(patching[2]);
    if (!Number.isSafeInteger(done) || !Number.isSafeInteger(total) || total <= 0 || done > total) return null;
    return { detail: `Preparing the game's source · patch ${done} of ${total}`, percent: Math.floor(done * 100 / total) };
  }
  if (/^-- (?:Configuring|Generating)/.test(value))
    return { detail: 'Getting your game ready', percent: null };
  if (/^Cloning into /.test(value))
    return { detail: 'Downloading setup files', percent: null };
  if (/^(?:Applying|Patching|Writing|Bundling) /.test(value))
    return { detail: 'Preparing game files', percent: null };
  return null;
}

// cmake/eclipse.cmake fetches these one at a time and git's own progress lines
// do not say which, so a task's activity remembers the one being downloaded.
const ECLIPSE_SOURCES = [['eclipse', 'Super Mario Eclipse'], ['bse', 'Better Sunshine Engine'],
  ['moveset', 'Better Sunshine Moveset'], ['shi', 'Sunshine Header Interface']];

function createActivityReader() {
  let source = null;
  return function read(line) {
    const value = cleanOutputLine(line).trim();
    const fetch = value.match(/^-- SMS_ECLIPSE: fetching (\w+) [a-f0-9]{40}$/);
    if (fetch) {
      const index = ECLIPSE_SOURCES.findIndex(([name]) => name === fetch[1]);
      source = index >= 0 ? `${ECLIPSE_SOURCES[index][1]} (${index + 1} of ${ECLIPSE_SOURCES.length})` : fetch[1];
      return { detail: `Downloading Eclipse sources · ${source}`, percent: null };
    }
    if (source && /^remote: (?:Enumerating|Counting|Compressing) objects/.test(value))
      return { detail: `Downloading Eclipse sources · ${source} · waiting for GitHub`, percent: null };
    const git = source && value.match(/^(Receiving objects|Resolving deltas|Updating files):\s*(\d{1,3})%(?:.*?,\s*([\d.]+ [KMG]iB)(?:\s*\|\s*([\d.]+ [KMG]iB\/s))?)?/);
    if (git) {
      const step = git[1] === 'Receiving objects' ? 'Downloading' : git[1] === 'Resolving deltas' ? 'Unpacking' : 'Writing';
      const size = git[3] ? ` · ${git[3]}${git[4] ? ` at ${git[4]}` : ''}` : '';
      return { detail: `${step} Eclipse sources · ${source}${size}`, percent: Math.min(100, Number(git[2])) };
    }
    const activity = activityFromLine(value);
    if (activity) source = null;
    return activity;
  };
}

// A task that did not exit by itself. Its last output is then whatever it
// printed before, not the reason. Linux: killed by the signal (the game's crash
// handler re-raises it). macOS: the game's handler exits with the shell's
// 128 + signal instead (platform/port_runtime.cpp: a re-raised signal hangs
// under Rosetta). Windows: an exception status, or a native program's crash as
// MSYS bash reports it (the signal in the second byte: 2816 for SIGSEGV).
const CRASH_SIGNALS = { SIGSEGV: 'a memory access error', SIGBUS: 'a memory access error', SIGILL: 'an illegal instruction',
  SIGFPE: 'a math error', SIGABRT: 'an internal error' };
// Signal numbers as macOS and MSYS (Cygwin) number them, and as Linux does.
const BSD_SIGNALS = { 4: 'SIGILL', 6: 'SIGABRT', 8: 'SIGFPE', 10: 'SIGBUS', 11: 'SIGSEGV' };
const LINUX_SIGNALS = { 4: 'SIGILL', 6: 'SIGABRT', 7: 'SIGBUS', 8: 'SIGFPE', 11: 'SIGSEGV' };
const WINDOWS_STATUSES = { 0xC0000005: 'SIGSEGV', 0xC00000FD: 'SIGSEGV', 0xC000001D: 'SIGILL', 0xC0000094: 'SIGFPE',
  0xC0000409: 'SIGABRT' };

function crashReason(code, signal, platform = process.platform) {
  let name = signal;
  if (!name && Number.isInteger(code))
    name = platform === 'win32' ? BSD_SIGNALS[code / 256] || WINDOWS_STATUSES[code >>> 0]
      : (platform === 'darwin' ? BSD_SIGNALS : LINUX_SIGNALS)[code - 128];
  return CRASH_SIGNALS[name] ? `crashed with ${CRASH_SIGNALS[name]}` : null;
}

// The line that best explains a failed task, from its last lines of output.
function failureReason(lines) {
  const recent = lines.map(line => cleanOutputLine(line).trim()).filter(Boolean);
  const cmake = recent.findLastIndex(line => /^CMake Error\b/.test(line));
  const detail = [];
  if (cmake >= 0) for (const line of recent.slice(cmake + 1)) {
    if (/^(?:Call Stack|CMake |-- )/.test(line)) break;
    detail.push(line);
  }
  const reason = detail.join(' ') || recent.findLast(line =>
    !/^(?:ninja: build stopped|make(?:\[\d+\])?: \*\*\*|-- Configuring incomplete|\d+ errors? generated|compilation terminated)/.test(line) &&
    /\berror\b|fatal|FAILED|failed|missing|not found|No such file|cannot|could not/i.test(line)) || recent.at(-1) || '';
  return reason.slice(0, 400).replace(/[.\s]+$/, '');
}

module.exports = { activityFromLine, cleanOutputLine, crashReason, createActivityReader, createLineReader, failureReason };
