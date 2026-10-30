// Makes the GIF test fixtures with the web page's own encoder (code-drawn, no artwork): node scripts/make_test_gifs.mjs
import { readFileSync, writeFileSync } from 'node:fs';
const html = readFileSync(new URL('../web-sync/index.html', import.meta.url), 'utf8');
const lib = new Function(html.split('<script>')[1].split('// ---- UI ----')[0] + '; return { gifFit, gifCheck, GIFS };')();
function make(w, h, n, name, budget) { // a few moving colored bars over a gradient
  const frames = [];
  for (let f = 0; f < n; f++) {
    const a = new Uint8ClampedArray(w * h * 4);
    for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
      const i = (y * w + x) * 4, bar = ((x + f * (w / n)) % w) < w / 6;
      a[i] = bar ? 255 : (x * 255 / w) | 0; a[i + 1] = bar ? 220 : (y * 255 / h) | 0; a[i + 2] = bar ? 40 : 120 + 60 * Math.sin((x + y + f * 9) / 11); a[i + 3] = 255;
    }
    frames.push(a);
  }
  const fit = lib.gifFit(frames, w, h, Array(n).fill(100), budget);
  writeFileSync(new URL('../sketch/test/fixtures/' + name, import.meta.url), fit.bytes);
  console.log(name, w + 'x' + h, fit.frames + ' frames', fit.colors + ' colors', fit.bytes.length + ' bytes', JSON.stringify(lib.gifCheck(fit.bytes)));
}
make(96, 96, 8, 'gif_96x96.gif', 60000);
make(128, 128, 16, 'gif_128x128.gif', 60000);
