// The art-direction palette, sampled from "The Last Signal" concept sheet (right footer swatches).
// Everything in the demo derives its colour from here: plants, house, terrain, sky and ink lines.
// Values are sRGB hex. This module has no browser dependencies so tools/make-house.mjs can use it too.

export const SWATCHES = {
  sky: '#3d7cc4',      // swatch 1: sea / sky blue
  leaf: '#4f9e4c',     // swatch 2: foliage green
  concrete: '#dcd3bf', // swatch 3: weathered concrete / paper
  rust: '#c4573a',     // swatch 4: rust, scarf red
  ink: '#2b3242',      // swatch 5: ink navy (lines, deep shade)
};

export const PALETTE = {
  ...SWATCHES,

  // Sky and atmosphere
  skyZenith: '#3a7fd0',
  skyHorizon: '#c4e4f4',
  haze: '#b4d7ec',
  sun: '#fff4dc',
  cloud: '#fbfbf6',
  cloudShade: '#a6c0de',
  mountain: '#6f9fcf',

  // Light: shadows are tinted toward the sky blue, never grey
  sunLight: '#fff1d6',
  shadowTint: '#6d8cc6',
  skyAmbient: '#9cc4ec',
  groundBounce: '#9fae74',

  // Foliage (lit / base / shaded)
  ivyLight: '#a3d45a',
  ivy: '#4f9e4c',
  ivyShadow: '#2c5f57',
  bushLight: '#b4d965',
  bush: '#5aa64e',
  bushShadow: '#2f6656',
  grassLight: '#b7da68',
  grass: '#6cae4c',
  grassShadow: '#3d7656',
  stem: '#6f5c43',
  flowerWhite: '#f7f4ea',
  flowerYellow: '#f2c94c',
  flowerCenter: '#e49a36',
  treeLight: '#a6d25e',
  tree: '#438f45',
  treeShadow: '#25544f',
  barkLight: '#a38d72',
  bark: '#7d6a56',
  barkShadow: '#5b5262',

  // House
  concreteDark: '#b9ae99',
  trim: '#cdc3ac',
  window: '#2b3242',
  door: '#a9472f',

  // Ground
  dirt: '#c2ae86',
};

// sRGB hex -> linear [r, g, b] (for glTF baseColorFactor and shader uniforms).
export function hexToLinear(hex) {
  const n = parseInt(hex.slice(1), 16);
  return [(n >> 16) & 255, (n >> 8) & 255, n & 255].map((c) => {
    const s = c / 255;
    return s <= 0.04045 ? s / 12.92 : Math.pow((s + 0.055) / 1.055, 2.4);
  });
}
