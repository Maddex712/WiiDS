'use strict';

// Shared by the main process and renderer so reloads and live output have the
// same history, including updates to a line that has not ended yet.
(function (root) {
  function createActivityLog(limit = 700) {
    let nextId = 0, sequence = 0, resetSequence = -1;
    const entries = new Map();
    function merge(entry) {
      if (entry.sequence <= resetSequence) return;
      const previous = entries.get(entry.id);
      if (!previous || entry.sequence > previous.sequence) entries.set(entry.id, entry);
      nextId = Math.max(nextId, entry.id);
      sequence = Math.max(sequence, entry.sequence);
      const ordered = [...entries.keys()].sort((a, b) => a - b);
      for (const id of ordered.slice(0, Math.max(0, ordered.length - limit))) entries.delete(id);
    }
    return {
      write(text, stream = 'launcher', previous = null) {
        const entry = { id: previous?.id ?? ++nextId, sequence: ++sequence, stream, text };
        merge(entry);
        return entry;
      },
      merge,
      reset(beforeSequence = sequence + 1) {
        if (beforeSequence <= resetSequence) return;
        for (const [id, entry] of entries) if (entry.sequence <= beforeSequence) entries.delete(id);
        sequence = Math.max(sequence, beforeSequence);
        resetSequence = beforeSequence;
      },
      resetSequence() { return resetSequence; },
      snapshot() { return [...entries.values()].sort((a, b) => a.id - b.id); }
    };
  }
  function formatEntry(entry) { return `[${entry.stream}] ${entry.text}`; }
  const api = { createActivityLog, formatEntry };
  if (typeof module === 'object' && module.exports) module.exports = api;
  else root.smsActivityLog = api;
})(typeof globalThis === 'object' ? globalThis : this);
