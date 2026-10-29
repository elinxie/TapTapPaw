// Tests for the layout editor model in index.html (no DOM): geometry, limits, undo/redo, templates, import/export.
// Run: node web-sync/test-editor.mjs
import { readFileSync } from 'node:fs';
import assert from 'node:assert/strict';

const html = readFileSync(new URL('./index.html', import.meta.url), 'utf8');
const code = html.split('<script>')[1].split('// ---- UI ----')[0];
const lib = new Function(code + '; return { pkt, LAYOUT, WIDGET_DEFS, TEMPLATES, SAMPLE, PREVIEW_ART, createEditor, validateLayout, layoutFromState };')();
const { LAYOUT, WIDGET_DEFS, TEMPLATES, createEditor, validateLayout } = lib;
const TYPES = Object.keys(WIDGET_DEFS);
const ids = (ed, pi = 0) => ed.state.pages[pi].w.map((w) => w.id);

assert.equal(LAYOUT.W, 240); assert.equal(LAYOUT.H, 320); // the board is portrait

// every template is a valid layout and survives load -> export unchanged in meaning
for (const t of TEMPLATES) {
  const text = JSON.stringify(t.layout);
  const v = validateLayout(text);
  assert.equal(v.ok, true, 'template ' + t.name + ': ' + v.err);
  const ed = createEditor();
  assert.equal(ed.load(text).ok, true);
  const out = ed.toJSON();
  assert.equal(out.ok, true, 'export of ' + t.name + ': ' + out.err);
  const norm = JSON.parse(text); norm.pg.forEach((p) => { if (!p.m && !p.w) p.w = []; (p.w || []).forEach((w) => { for (const k of ['k', 'z', 'a', 'r', 'f', 'q']) if (w[k] === 0) delete w[k]; if (w.o === 255) delete w.o; }); }); // defaults are not written // the editor always writes an explicit (empty) widget list
  assert.deepEqual(JSON.parse(out.text), norm, 'round trip ' + t.name);
  const again = createEditor(); again.load(out.text); assert.equal(again.toJSON().text, out.text, 'export is a fixed point: ' + t.name);
}

// a fresh editor has one empty weather-background page; widget defaults are valid for every type and the export is minimal
{
  const ed = createEditor();
  assert.equal(ed.state.pages.length, 1);
  assert.equal(ed.toJSON().text, '{"v":1,"pg":[{"w":[]}]}');
  TYPES.forEach((t, i) => { if (i && i % 12 === 0) ed.addPage(); const r = ed.addWidget(Math.floor(i / 12), t); assert.equal(r.ok, true, t); }); // 12 per page
  const out = ed.toJSON();
  assert.equal(out.ok, true, out.err);
  assert.ok(out.size < 2000, 'one of each widget is small: ' + out.size);
  for (const w of ed.state.pages.flatMap((p) => p.w)) {
    const d = WIDGET_DEFS[w.t];
    assert.ok(w.x >= 0 && w.y >= 0 && w.x + w.w <= LAYOUT.W && w.y + w.h <= LAYOUT.H, w.t + ' inside the screen');
    assert.equal(w.w, d.w); assert.equal(w.h, d.h);
  }
  assert.equal(JSON.parse(out.text).pg[0].w.find((w) => w.t === 'tx').s, 'Text');
}

// geometry: grid snap, clamp, fixed sizes, minimum size
{
  const ed = createEditor();
  const a = ed.addWidget(0, 'dc').id, b = ed.addWidget(0, 'ac').id;
  ed.setGeom(a, { x: 13, y: 10, w: 101, h: 30 });
  let w = ed.find(a).w;
  assert.deepEqual([w.x, w.y, w.w, w.h], [12, 12, 100, 32], 'snapped to the 4 px grid');
  ed.setGeom(a, { x: -50, y: 999 });
  w = ed.find(a).w; assert.deepEqual([w.x, w.y], [0, LAYOUT.H - w.h], 'clamped to the screen');
  ed.setGeom(a, { x: 300, w: 200 });
  w = ed.find(a).w; assert.ok(w.x + w.w <= LAYOUT.W && w.w === 200 && w.x === LAYOUT.W - 200, 'width kept, x pulled back inside');
  ed.setGeom(a, { w: 1, h: 1 });
  w = ed.find(a).w; assert.ok(w.w >= 4 && w.h >= 2, 'minimum size');
  ed.setGeom(a, { x: 13.4, y: 7 }, { noSnap: true });
  w = ed.find(a).w; assert.equal(w.x, 13, 'noSnap keeps whole pixels');
  ed.setGeom(b, { x: 10, y: 10, w: 500, h: 500 });
  const c = ed.find(b).w; assert.deepEqual([c.w, c.h], [90, 90], 'analog clock keeps its fixed size');
  ed.grid = 8; ed.setGeom(a, { x: 13, y: 13 }); w = ed.find(a).w; assert.deepEqual([w.x, w.y], [16, 16], 'grid is changeable');
  assert.equal(ed.setGeom('nope', {}), false);
}

// z-order
{
  const ed = createEditor();
  const [a, b, c] = ['tc', 'tx', 'sh'].map((t) => ed.addWidget(0, t).id);
  assert.deepEqual(ids(ed), [a, b, c]);
  ed.z(a, 'front'); assert.deepEqual(ids(ed), [b, c, a]);
  ed.z(a, 'back'); assert.deepEqual(ids(ed), [a, b, c]);
  ed.z(a, 'up'); assert.deepEqual(ids(ed), [b, a, c]);
  ed.z(a, 'down'); assert.deepEqual(ids(ed), [a, b, c]);
  assert.equal(ed.z(a, 'down'), false, 'already at the back');
  assert.equal(ed.z(c, 'front'), false, 'already on top');
  assert.equal(ed.toLayout().pg[0].w[2].t, 'sh', 'export keeps the z-order: last = on top');
}

// duplicate and delete
{
  const ed = createEditor();
  const a = ed.addWidget(0, 'tx').id;
  ed.setProp(a, 's', 'Hello'); ed.setProp(a, 'c', '#ff0000');
  const r = ed.duplicate(a);
  assert.equal(r.ok, true);
  const orig = ed.find(a).w, copy = ed.find(r.id).w;
  assert.notEqual(copy.id, orig.id); assert.equal(copy.s, 'Hello'); assert.equal(copy.c, '#ff0000');
  assert.equal(copy.x, Math.min(orig.x + 8, LAYOUT.W - copy.w)); assert.equal(ed.state.pages[0].w[1].id, r.id, 'copy sits right above the original');
  copy.s = 'changed'; assert.equal(ed.find(a).w.s, 'Hello', 'the copy is independent');
  assert.equal(ed.remove(r.id), true); assert.equal(ed.find(r.id), null); assert.equal(ed.remove(r.id), false);
}

// limits
{
  const ed = createEditor();
  for (let i = 0; i < 12; i++) assert.equal(ed.addWidget(0, 'sh').ok, true);
  const over = ed.addWidget(0, 'sh'); assert.equal(over.ok, false); assert.match(over.why, /12 widgets per page/);
  assert.equal(ed.duplicate(ed.state.pages[0].w[0].id).ok, false);
  for (let i = 0; i < 5; i++) assert.equal(ed.addPage().ok, true);
  assert.equal(ed.addPage().ok, false); assert.match(ed.addPage().why, /6 pages/);
  let n = 12;
  for (let pi = 1; pi < 4; pi++) for (let i = 0; i < 12 && n < 40; i++) { assert.equal(ed.addWidget(pi, 'sh').ok, true); n++; }
  assert.equal(n, 40); const t = ed.addWidget(4, 'sh'); assert.equal(t.ok, false); assert.match(t.why, /40 widgets in total/);
  const out = ed.toJSON(); assert.equal(out.ok, true, out.err);
  assert.ok(out.size <= 4096, 'full layout fits the board limit: ' + out.size);
}
{
  // text budget: 512 bytes of label text in total (each label costs its bytes + 1)
  const ed = createEditor(); ed.addPage(); ed.addPage();
  let made = 0;
  for (let pi = 0; pi < 3; pi++) for (let i = 0; i < 8; i++) { const r = ed.addWidget(pi, 'tx'); if (r.ok) { made++; assert.equal(ed.setProp(r.id, 's', 'abcdefghijklmnopqrstuvwx').ok || true, true); } }
  assert.ok(ed.textBytes() <= 511, 'never above the pool: ' + ed.textBytes());
  assert.ok(ed.toJSON().ok, 'still exportable: ' + ed.toJSON().err);
  const last = ed.state.pages.flatMap((p) => p.w).filter((w) => w.t === 'tx').pop();
  const r = ed.setProp(last.id, 's', 'x'.repeat(24)); assert.ok(r.ok === true || /budget/.test(r.why));
}

// text property rules
{
  const ed = createEditor(); const a = ed.addWidget(0, 'tx').id;
  assert.equal(ed.setProp(a, 's', 'x'.repeat(25)).ok, false);
  assert.equal(ed.setProp(a, 's', 'é'.repeat(13)).ok, false, '26 bytes');
  assert.equal(ed.setProp(a, 's', 'é'.repeat(12)).ok, true, '24 bytes');
  assert.equal(ed.setProp(a, 's', 'a\nb').ok, false);
  assert.equal(ed.setProp(a, 's', '\ud800').ok, false);
  assert.equal(ed.setProp(a, 's', '').ok, true);
  assert.equal(ed.toJSON().ok, false, 'empty label text is rejected by the board schema, and the editor says so');
  assert.equal(ed.setProp('nope', 's', 'x').ok, false);
  ed.setProp(a, 's', 'ok'); ed.setProp(a, 'c', '#abc'); ed.setProp(a, 'c', '');
  assert.equal(ed.find(a).w.c, undefined, 'empty clears an option');
}

// main page
{
  const ed = createEditor();
  assert.equal(ed.addMainPage().ok, true); assert.equal(ed.state.pages[0].main, true);
  assert.equal(ed.addMainPage().ok, false);
  assert.equal(ed.addWidget(0, 'dc').ok, false, 'no widgets on the original screen');
  assert.equal(ed.addWidget(1, 'dc').ok, true);
  assert.deepEqual(ed.toLayout().pg[0], { m: 1 });
  assert.equal(ed.toJSON().ok, true);
  ed.removePage(0); assert.equal(ed.addMainPage().ok, true, 'can be added again after removal');
}

// pages: reorder, remove, background, rotation
{
  const ed = createEditor(); ed.addPage(); ed.addPage();
  ed.setBg(0, { t: 'b', i: 2 }); ed.setBg(1, { t: 's', c: '#102030' }); ed.setBg(2, { t: 'g', c: '#000', d: '#fff' });
  ed.movePage(0, 1); assert.deepEqual(ed.state.pages.map((p) => p.bg.t), ['s', 'b', 'g']);
  assert.equal(ed.movePage(0, -1), false); assert.equal(ed.movePage(2, 1), false);
  assert.equal(ed.removePage(1).ok, true); assert.equal(ed.state.pages.length, 2);
  ed.removePage(0); assert.equal(ed.removePage(0).ok, false, 'the last page stays');
  ed.setRot(5); assert.equal(ed.state.rot, 5); ed.setRot(1); assert.equal(ed.state.rot, 3); ed.setRot(9999); assert.equal(ed.state.rot, 600); ed.setRot(0); assert.equal(ed.state.rot, 0);
  assert.equal(ed.toJSON().ok, true);
}

// undo / redo: one step per action, a drag is one step, a new edit clears redo
{
  const ed = createEditor();
  assert.equal(ed.canUndo(), false); assert.equal(ed.undo(), false); assert.equal(ed.redo(), false);
  const a = ed.addWidget(0, 'dc').id;
  const x0 = ed.find(a).w.x;
  ed.begin(); ed.setGeom(a, { x: 20 }); ed.setGeom(a, { x: 40 }); ed.setGeom(a, { x: 60 }); ed.end();
  assert.equal(ed.find(a).w.x, 60);
  ed.undo(); assert.equal(ed.find(a).w.x, x0, 'the whole drag is undone in one step');
  ed.redo(); assert.equal(ed.find(a).w.x, 60);
  ed.undo(); assert.equal(ed.canRedo(), true);
  ed.addWidget(0, 'tc'); assert.equal(ed.canRedo(), false, 'a new edit clears redo');
  ed.undo(); ed.undo(); assert.equal(ed.state.pages[0].w.length, 0, 'back to the empty page');
  assert.equal(ed.canUndo(), false);
  let changes = 0; ed.onChange(() => changes++); ed.redo(); assert.ok(changes >= 1, 'listeners are called');
  // history is capped
  const e2 = createEditor();
  for (let i = 0; i < 150; i++) e2.setRot(i % 2 ? 5 : 10);
  let steps = 0; while (e2.undo()) steps++; assert.equal(steps, 100);
  // ids stay unique through undo/redo/duplicate
  const e3 = createEditor(); const p = e3.addWidget(0, 'tc').id; const q = e3.duplicate(p).id; e3.undo(); const r = e3.duplicate(p).id;
  assert.notEqual(q, r, 'ids are never reused'); assert.notEqual(p, r);
}

// import / export
{
  const ed = createEditor(); ed.addWidget(0, 'dc');
  const before = JSON.stringify(ed.state);
  assert.deepEqual(ed.load('{"v":2,"pg":[{}]}'), { ok: false, err: 'unsupported version' });
  assert.equal(ed.load('garbage').ok, false); assert.equal(ed.load('').ok, false);
  assert.equal(JSON.stringify(ed.state), before, 'a rejected import changes nothing');
  assert.equal(ed.load(readFileSync(new URL('../docs/layout-example.json', import.meta.url), 'utf8').trim()).ok, true);
  assert.equal(ed.state.pages.length, 3); assert.equal(ed.state.pages[0].main, true);
  ed.undo(); assert.equal(ed.state.pages.length, 1, 'an import can be undone');
  // text sent to the board is exactly what was validated and is what the size shows
  const out = createEditor(TEMPLATES[5].layout).toJSON();
  assert.equal(out.ok, true); assert.equal(new TextEncoder().encode(out.text).length, out.size);
  // exported ids are not part of the file
  assert.ok(!/"id"/.test(out.text));
  // hand-written JSON with a field the editor does not know survives validation but is not exported (forward compat is the firmware's business)
  const e = createEditor(); assert.equal(e.load('{"v":1,"pg":[{"w":[{"t":"tc","x":0,"y":0,"w":10,"h":10,"zzz":1}]}]}').ok, true);
  assert.ok(!/zzz/.test(e.toJSON().text));
}

// sample data is complete for the preview and dry run
for (const k of ['h', 'm', 's', 'group', 'tempC', 'humidity', 'title', 'artist', 'media', 'cpu', 'mem', 'batt', 'taps']) assert.ok(k in lib.SAMPLE, k);
// preview art generated from the firmware images is present and well formed
for (const k of ['wall0', 'wall1', 'wall2', 'wall3', 'wall4', 'wall5', 'cat', 'eyes', 'sleepy', 'clock', 'hh', 'mh', 'sh', 'i0', 'i1', 'i2', 'i3', 'i4', 'i5'])
  assert.match(lib.PREVIEW_ART[k], /^data:image\/(png|jpeg);base64,[A-Za-z0-9+/=]{100,}$/, 'preview art ' + k);
console.log('editor tests ok');
{
  const ed = createEditor(); ed.addWidget(0, 'tc'); ed.clearHistory();
  assert.equal(ed.canUndo(), false); assert.equal(ed.canRedo(), false); assert.equal(ed.state.pages[0].w.length, 1, 'clearHistory keeps the content');
  console.log('editor history clear ok');
}
{
  // the typing cat: fixed 240x132, placed full width, valid layout
  const ed = createEditor(); const r = ed.addWidget(0, 'ct');
  assert.equal(r.ok, true); const w = ed.find(r.id).w;
  assert.deepEqual([w.x, w.w, w.h], [0, 240, 132]);
  ed.setGeom(r.id, { x: 50, w: 100 }); assert.deepEqual([ed.find(r.id).w.x, ed.find(r.id).w.w], [0, 240], 'cannot move sideways or resize');
  assert.equal(ed.toJSON().ok, true);
  for (const k of ['pawL', 'pawD', 'pawU']) assert.match(lib.PREVIEW_ART[k], /^data:image\/png;base64,/);
  console.log('typing cat ok');
}
