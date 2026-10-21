'use strict';

// Lets a game controller drive the launcher: the left stick and d-pad move
// focus to the nearest control on screen, A activates it (stepping through a
// list's choices), left and right move a slider, and the remaining buttons
// call actions supplied by the renderer.
(function (root) {
  // Standard Gamepad mapping: https://w3c.github.io/gamepad/#remapping
  const BUTTONS = { confirm: 0, back: 1, alternate: 2, previous: 4, next: 5, view: 8, menu: 9, up: 12, down: 13, left: 14, right: 15 };
  const DIRECTIONS = ['up', 'down', 'left', 'right'];
  const FOCUSABLE = 'button, select, input:not([type="hidden"]), textarea, summary, a[href], [tabindex]:not([tabindex="-1"])';

  function pressed(gamepad, index) {
    const button = gamepad.buttons[index];
    return Boolean(button && (button.pressed || button.value > 0.5));
  }

  function heldInputs(gamepads, deadzone = 0.5) {
    const held = new Set();
    for (const gamepad of gamepads) {
      if (!gamepad?.connected) continue;
      for (const [name, index] of Object.entries(BUTTONS)) if (pressed(gamepad, index)) held.add(name);
      // Only the stronger axis counts, so a slightly diagonal stick still moves in one direction.
      const [x = 0, y = 0] = gamepad.axes;
      if (Math.max(Math.abs(x), Math.abs(y)) < deadzone) continue;
      if (Math.abs(x) > Math.abs(y)) held.add(x < 0 ? 'left' : 'right');
      else held.add(y < 0 ? 'up' : 'down');
    }
    return held;
  }

  function scrollAxis(gamepads, deadzone = 0.25) {
    let strongest = 0;
    for (const gamepad of gamepads) {
      const y = gamepad?.connected ? gamepad.axes[3] || 0 : 0;
      if (Math.abs(y) > Math.abs(strongest)) strongest = y;
    }
    return Math.abs(strongest) < deadzone ? 0 : strongest;
  }

  // Fires each input once when pressed; directions repeat while held. Inputs
  // held while quiet stay silent until released, so a button still down from
  // playing the game does nothing when the launcher gets focus back.
  function createRepeater({ delay = 380, interval = 110 } = {}) {
    const states = new Map();
    return function update(held, now, quiet = false) {
      const fired = [];
      for (const name of states.keys()) if (!held.has(name)) states.delete(name);
      for (const name of held) {
        const state = states.get(name);
        if (quiet) states.set(name, { next: Infinity });
        else if (!state) { states.set(name, { next: now + delay }); fired.push(name); }
        else if (DIRECTIONS.includes(name) && now >= state.next) { state.next = now + interval; fired.push(name); }
      }
      return fired;
    };
  }

  // Picks the rectangle to move to from `from`. Candidates must lie past the
  // current control's centre and within about 45 degrees of the direction, so
  // nothing far off to the side is picked; aligned ones are preferred over
  // closer diagonal ones.
  function nextInDirection(from, candidates, direction) {
    const vertical = direction === 'up' || direction === 'down';
    const fx = (from.left + from.right) / 2, fy = (from.top + from.bottom) / 2;
    let best = -1, bestScore = Infinity;
    candidates.forEach((to, index) => {
      const ahead = { up: to.bottom < fy, down: to.top > fy, left: to.right < fx, right: to.left > fx }[direction];
      if (!ahead) return;
      const gap = Math.max(0, { up: from.top - to.bottom, down: to.top - from.bottom, left: from.left - to.right, right: to.left - from.right }[direction]);
      const cross = vertical ? Math.max(0, to.left - from.right, from.left - to.right) : Math.max(0, to.top - from.bottom, from.top - to.bottom);
      if (cross > gap + 24) return;
      // Candidates whose centre lies alongside the current control tie, so the first in the row wins.
      const centre = vertical ? (to.left + to.right) / 2 : (to.top + to.bottom) / 2;
      const offset = vertical ? Math.max(0, from.left - centre, centre - from.right) : Math.max(0, from.top - centre, centre - from.bottom);
      const score = gap + cross * 2 + offset * 0.01;
      if (score < bestScore) { best = index; bestScore = score; }
    });
    return best;
  }

  function start({ document, window, regions = '', defaultFocus = () => [], paused = () => false, actions = {} }) {
    const repeat = createRepeater();
    let frame = 0, lastFrame = 0;

    const scope = () => [...document.querySelectorAll('dialog[open]')].pop() || document.body;
    // Small or hidden inputs, like switch checkboxes, navigate by their visible label.
    const region = element => (regions && element.closest(regions)) || element;
    // checkVisibility also rejects the contents of a closed <details>, which still have a layout box.
    const usable = element => !element.disabled && !element.closest('[inert]') &&
      element.checkVisibility({ visibilityProperty: true });
    const candidates = () => [...scope().querySelectorAll(FOCUSABLE)].filter(usable);

    function setActive(active) { document.body.classList.toggle('gamepad-active', active); }
    document.addEventListener('pointerdown', () => setActive(false), true);
    document.addEventListener('mousemove', event => { if (event.movementX || event.movementY) setActive(false); }, true);

    function focus(element) {
      element.focus({ preventScroll: true });
      region(element).scrollIntoView({ block: 'nearest', inline: 'nearest' });
    }

    function focusDefault(available) {
      const preferred = defaultFocus().find(element => available.includes(element)) || available[0];
      if (preferred) focus(preferred);
    }

    function cycle(select, step) {
      const options = [...select.options];
      for (let i = 1; i <= options.length; i++) {
        const index = (select.selectedIndex + step * i + options.length * i) % options.length;
        if (options[index].disabled) continue;
        if (index === select.selectedIndex) return;
        select.selectedIndex = index;
        select.dispatchEvent(new window.Event('change', { bubbles: true }));
        return;
      }
    }

    function slide(range, step) {
      const before = range.value;
      if (step > 0) range.stepUp(); else range.stepDown();
      if (range.value === before) return;
      range.dispatchEvent(new window.Event('input', { bubbles: true }));
      range.dispatchEvent(new window.Event('change', { bubbles: true }));
    }

    function scrollable(element) {
      const style = window.getComputedStyle(element);
      return element.scrollHeight > element.clientHeight + 1 && /(auto|scroll)/.test(style.overflowY);
    }

    function scroll(amount) {
      const area = scope();
      let target = document.activeElement;
      while (target && target !== area && !scrollable(target)) target = target.parentElement;
      if (!target || !scrollable(target)) target = [...area.querySelectorAll('*')].find(scrollable);
      if (target) target.scrollTop += amount;
    }

    function handle(name) {
      setActive(true);
      const available = candidates();
      const focused = available.includes(document.activeElement) ? document.activeElement : null;
      if (!focused && (DIRECTIONS.includes(name) || name === 'confirm')) return focusDefault(available);
      if ((name === 'left' || name === 'right') && focused.type === 'range') slide(focused, name === 'right' ? 1 : -1);
      else if (DIRECTIONS.includes(name)) {
        const others = available.filter(element => region(element) !== region(focused));
        const index = nextInDirection(region(focused).getBoundingClientRect(),
          others.map(element => region(element).getBoundingClientRect()), name);
        if (index >= 0) focus(others[index]);
      } else if (name === 'confirm') {
        if (focused.tagName === 'SELECT') cycle(focused, 1);
        else focused.click();
      } else if (name === 'alternate' && focused?.tagName === 'SELECT') cycle(focused, -1);
      else actions[name]?.();
      // An action that hides the focused control, like a setup step's Back, should not strand focus.
      const after = candidates();
      if (!after.includes(document.activeElement)) focusDefault(after);
    }

    function loop(now) {
      frame = 0;
      const gamepads = [...(window.navigator.getGamepads?.() || [])];
      if (!gamepads.some(gamepad => gamepad?.connected)) return;
      const quiet = !document.hasFocus() || paused();
      for (const name of repeat(heldInputs(gamepads), now, quiet)) handle(name);
      const axis = quiet ? 0 : scrollAxis(gamepads);
      if (axis) scroll(axis * Math.min(50, now - lastFrame) * 0.9);
      lastFrame = now;
      frame = window.requestAnimationFrame(loop);
    }
    function wake() { if (!frame) frame = window.requestAnimationFrame(loop); }
    window.addEventListener('gamepadconnected', wake);
    wake();
  }

  const api = { heldInputs, scrollAxis, createRepeater, nextInDirection, start };
  if (typeof module === 'object' && module.exports) module.exports = api;
  else root.smsGamepadNav = api;
})(typeof globalThis === 'object' ? globalThis : this);
