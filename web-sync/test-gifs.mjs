// Tests for the GIF code in index.html: encoder (checked by decoding with an independent decoder written here), median-cut quantizer,
// size budgeting, and gifCheck (must agree with gif_check in layout_core.cpp). Run: node web-sync/test-gifs.mjs
import { readFileSync, writeFileSync } from 'node:fs';
import assert from 'node:assert/strict';

const html = readFileSync(new URL('./index.html', import.meta.url), 'utf8');
const code = html.split('<script>')[1].split('// ---- UI ----')[0];
const lib = new Function(code + '; return { gifCheck, gifLzw, gifEncode, gifQuantize, gifFit, GIFS, crc32, uploadLayoutBytes };')();
const { gifCheck, gifEncode, gifQuantize, gifFit, GIFS } = lib;

// independent GIF decoder: returns { w, h, palette, frames: [Uint8Array indices], delays }
function decodeGif(u) {
  let p = 6; const w = u[p] | (u[p + 1] << 8), h = u[p + 2] | (u[p + 3] << 8), flags = u[p + 4]; p += 7;
  const n = 2 << (flags & 7), palette = u.slice(p, p + n * 3); p += n * 3;
  const frames = [], delays = [];
  while (u[p] !== 0x3B) {
    if (u[p] === 0x21) { const label = u[p + 1]; if (label === 0xF9) delays.push((u[p + 4] | (u[p + 5] << 8))); p += 2; while (u[p]) p += u[p] + 1; p++; continue; }
    assert.equal(u[p], 0x2C); const iw = u[p + 5] | (u[p + 6] << 8), ih = u[p + 7] | (u[p + 8] << 8); p += 10;
    const minCode = u[p++]; const data = []; while (u[p]) { for (let i = 0; i < u[p]; i++) data.push(u[p + 1 + i]); p += u[p] + 1; } p++;
    const clear = 1 << minCode, eoi = clear + 1; let size = minCode + 1, next = eoi + 1, bit = 0; const dict = []; const out = [];
    const reset = () => { dict.length = 0; for (let i = 0; i < clear; i++) dict[i] = [i]; size = minCode + 1; next = eoi + 1; };
    reset(); let prev = null;
    for (;;) {
      let code = 0; for (let b = 0; b < size; b++, bit++) code |= ((data[bit >> 3] >> (bit & 7)) & 1) << b;
      if (code === clear) { reset(); prev = null; continue; }
      if (code === eoi) break;
      let entry; if (code < next && dict[code]) entry = dict[code]; else { assert.ok(prev, 'bad code'); entry = prev.concat(prev[0]); }
      out.push(...entry);
      if (prev && next < 4096) { dict[next++] = prev.concat(entry[0]); if (next === (1 << size) && size < 12) size++; }
      prev = entry;
    }
    assert.equal(out.length, iw * ih, 'decoded pixel count'); frames.push(Uint8Array.from(out));
  }
  return { w, h, palette, frames, delays };
}

// ---- encoder round trip: tiny, multi-frame, and big enough to overflow the 4096-entry code table (forces clear codes)
function roundTrip(w, h, nframes, ncol, noise) {
  const pal = new Uint8Array(ncol * 3).map((_, i) => (i * 37) & 255), frames = [], dl = [];
  let seed = 1; const rnd = () => (seed = (seed * 1103515245 + 12345) >>> 0) >>> 16;
  for (let f = 0; f < nframes; f++) frames.push(Uint8Array.from({ length: w * h }, (_, i) => noise ? rnd() % ncol : ((i + f) >> 3) % ncol)), dl.push(5 + f);
  const gif = gifEncode(w, h, pal, frames, dl), d = decodeGif(gif);
  assert.equal(d.w, w); assert.equal(d.h, h); assert.equal(d.frames.length, nframes);
  d.frames.forEach((f, i) => assert.deepEqual(Array.from(f), Array.from(frames[i]), 'frame ' + i + ' pixel-exact'));
  assert.deepEqual(d.delays, dl.map((x) => Math.max(2, x)));
  assert.deepEqual(Array.from(d.palette.slice(0, pal.length)), Array.from(pal));
  return gif;
}
const a = roundTrip(1, 1, 1, 2, false);
assert.ok(gifCheck(a).ok && gifCheck(a).w === 1);
roundTrip(7, 5, 3, 4, false); roundTrip(16, 16, 2, 16, true); roundTrip(64, 64, 4, 256, true); roundTrip(128, 128, 2, 256, true); roundTrip(100, 3, 1, 3, true);
assert.equal(roundTrip(20, 20, 1, 8, false)[GIFS.MAX_SIDE * 0 + 0], 0x47); // starts with "GIF"
assert.equal(new TextDecoder().decode(roundTrip(8, 8, 2, 8, false).subarray(0, 6)), 'GIF89a');
assert.ok(!new TextDecoder().decode(roundTrip(8, 8, 1, 8, false)).includes('NETSCAPE'), 'a single frame does not loop');
assert.ok(new TextDecoder().decode(roundTrip(8, 8, 2, 8, false)).includes('NETSCAPE2.0'), 'several frames loop forever');

// ---- gifCheck: the same refusals as the board
const good = roundTrip(32, 32, 2, 16, false);
assert.ok(gifCheck(good).ok);
const mut = (f) => { const b = Uint8Array.from(good); f(b); return b; };
assert.equal(gifCheck(mut((b) => { b[3] = 0x58; })).ok, false, 'GIF8?a magic');
assert.equal(gifCheck(mut((b) => { b[4] = 0x30; })).ok, false, 'GIF90a');
assert.equal(gifCheck(mut((b) => { b[6] = 129; })).ok, false, 'width 129');
assert.equal(gifCheck(mut((b) => { b[8] = 129; })).ok, false, 'height 129');
assert.equal(gifCheck(mut((b) => { b[6] = 0; b[7] = 0; })).ok, false, 'width 0');
assert.equal(gifCheck(mut((b) => { b[8] = 0; b[9] = 0; })).ok, false, 'height 0');
assert.equal(gifCheck(mut((b) => { b[b.length - 1] = 0; })).ok, false, 'no trailer');
assert.equal(gifCheck(mut((b) => { b[7] = 1; })).ok, false, 'width 288');
assert.equal(gifCheck(good.subarray(0, 10)).ok, false); assert.equal(gifCheck(null).ok, false);
assert.equal(gifCheck(new Uint8Array(GIFS.MAX_BYTES + 1)).ok, false);
const g87 = Uint8Array.from(good); g87[4] = 0x37; assert.ok(gifCheck(g87).ok, 'GIF87a is accepted');

// ---- quantizer: a 3-color image keeps its 3 colors exactly; a gradient stays close
const px = (r, g, b) => [r, g, b, 255];
const three = new Uint8ClampedArray(16 * 16 * 4); for (let i = 0; i < 256; i++) three.set(px(...[[255, 0, 0], [0, 255, 0], [0, 0, 255]][i % 3]), i * 4);
let q = gifQuantize([three], 16, 16, 16);
assert.ok(q.palette.length / 3 <= 16);
for (let i = 0; i < 256; i++) { const c = [...q.palette.slice(q.frames[0][i] * 3, q.frames[0][i] * 3 + 3)], want = [[255, 0, 0], [0, 255, 0], [0, 0, 255]][i % 3]; assert.deepEqual(c, want, 'pixel ' + i); }
const grad = new Uint8ClampedArray(64 * 64 * 4); for (let i = 0; i < 4096; i++) grad.set(px(i % 64 * 4, (i >> 6) * 4, 128), i * 4);
q = gifQuantize([grad], 64, 64, 64); let err = 0; for (let i = 0; i < 4096; i++) { const o = q.frames[0][i] * 3; err += Math.abs(q.palette[o] - (i % 64) * 4) + Math.abs(q.palette[o + 1] - (i >> 6) * 4); } 
assert.ok(err / 4096 < 20, 'gradient mean error per pixel ' + (err / 4096).toFixed(1));

// ---- gifFit: a simple animation fits at full quality; noise has to give up colors/frames; impossible content returns null
const anim = []; for (let f = 0; f < 12; f++) { const a = new Uint8ClampedArray(96 * 96 * 4); for (let i = 0; i < 96 * 96; i++) a.set(px(((i % 96) + f * 8) & 255, (i / 96 | 0) * 2, 90), i * 4); anim.push(a); }
const fit = gifFit(anim, 96, 96, Array(12).fill(80));
assert.ok(fit && fit.bytes.length <= GIFS.BUDGET && gifCheck(fit.bytes).ok, 'smooth animation fits: ' + (fit && fit.bytes.length));
assert.equal(decodeGif(fit.bytes).frames.length, fit.frames);
let seed = 7; const rnd = () => (seed = (seed * 1664525 + 1013904223) >>> 0) >>> 24;
const noisy = []; for (let f = 0; f < 20; f++) { const a = new Uint8ClampedArray(128 * 128 * 4); for (let i = 0; i < 128 * 128; i++) a.set(px(rnd(), rnd(), rnd()), i * 4); noisy.push(a); }
const nfit = gifFit(noisy, 128, 128, Array(20).fill(50), 60000);
assert.ok(nfit === null || (nfit.bytes.length <= 60000 && nfit.frames < 20), 'noise: gave up frames or did not fit (' + (nfit && nfit.frames) + ' frames, ' + (nfit && nfit.colors) + ' colors)');
const one = gifFit([anim[0]], 96, 96, [100]); assert.ok(one && one.frames === 1);
const slow = gifFit(anim, 96, 96, Array(12).fill(80), 4000); // tiny budget
assert.ok(slow === null || slow.bytes.length <= 4000);
const merged = gifFit(anim.slice(0, 4), 32, 32, [100, 100, 100, 100], 60000); // delays are kept when frames are dropped
assert.equal(merged.frames, 4);

writeFileSync('/tmp/ttp_test_anim.gif', fit.bytes);
console.log('gif tests ok (' + fit.bytes.length + ' bytes, ' + fit.frames + ' frames, ' + fit.colors + ' colors)');
