'use strict';
// Graphics presets: Low, Medium, High and Ultra set every picture setting at
// once, and Steam Deck suits the Deck's 1280×800 screen, as PC games do.
// Changing any of those settings afterwards makes the preset Custom.
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.SmsPresets = factory();
})(typeof self !== 'undefined' ? self : this, () => {
  // The settings every preset sets (port.js normalizeSettings): picture
  // sharpness (the internal resolution), anti-aliasing, FXAA, texture
  // filtering, sharpening, scaling and the heat-wave effect.
  const KEYS = ['resolution', 'msaa', 'fxaa', 'anisotropic', 'sharpen', 'presentFilter', 'heatHaze'];
  const PRESETS = [
    { id: 'low', label: 'Low', detail: 'For older or integrated graphics.',
      values: { resolution: 1, msaa: 0, fxaa: false, anisotropic: 0, sharpen: 0, presentFilter: 'bilinear', heatHaze: false } },
    { id: 'medium', label: 'Medium', detail: 'Sharper, with light smoothing.',
      values: { resolution: 2, msaa: 0, fxaa: true, anisotropic: 4, sharpen: 0, presentFilter: 'bilinear', heatHaze: true } },
    { id: 'high', label: 'High', detail: 'Very sharp, with anti-aliasing.',
      values: { resolution: 3, msaa: 4, fxaa: true, anisotropic: 8, sharpen: 0, presentFilter: 'bilinear', heatHaze: true } },
    { id: 'ultra', label: 'Ultra', detail: 'Everything at its best, for a strong graphics card.',
      values: { resolution: 4, msaa: 8, fxaa: true, anisotropic: 16, sharpen: 0, presentFilter: 'bilinear', heatHaze: true } },
    // The Deck's screen is 1280×800 (16:10): 2× is already sharper than it shows.
    { id: 'steamdeck', label: 'Steam Deck', detail: 'Made for the Deck’s 16:10 screen and battery: full screen at 2×.',
      values: { resolution: 2, msaa: 0, fxaa: true, anisotropic: 4, sharpen: 0, presentFilter: 'bilinear', heatHaze: true,
        widescreen: '16:10', fullscreen: true } }
  ];
  const CUSTOM = { id: 'custom', label: 'Custom', detail: 'Your own choices below.' };
  const CHOICES = [...PRESETS, CUSTOM];

  function find(id) { return PRESETS.find(preset => preset.id === id) || null; }

  // Whether these settings are still what the preset sets.
  function matches(settings, id) {
    const preset = find(id);
    return Boolean(preset && settings && Object.entries(preset.values).every(([key, value]) => settings[key] === value));
  }

  // The preset to show for these settings: the chosen one while they still
  // match it, else Custom.
  function reconcile(settings) {
    const id = settings && settings.graphicsPreset;
    return matches(settings, id) ? id : 'custom';
  }

  // These settings with the preset's values (Custom changes nothing).
  function apply(settings, id) {
    const preset = find(id);
    return preset ? { ...settings, ...preset.values, graphicsPreset: id } : { ...settings, graphicsPreset: 'custom' };
  }

  return { KEYS, PRESETS, CHOICES, find, matches, reconcile, apply };
});
