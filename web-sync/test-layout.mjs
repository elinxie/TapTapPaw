// Tests for the layout code in index.html (schema validator, CRC32, upload packets, ack decoding, upload driver).
// The validator is also run against sketch/test/host/layout-cases.tsv, the same table the C++ firmware parser is tested with.
// Run: node web-sync/test-layout.mjs
import { readFileSync } from 'node:fs';
import assert from 'node:assert/strict';

const html = readFileSync(new URL('./index.html', import.meta.url), 'utf8');
const code = html.split('<script>')[1].split('// ---- UI ----')[0];
const lib = new Function(code + '; return { pkt, LAYOUT, LAYOUT_ERRORS, crc32, validateLayout, layoutBeginPacket, layoutChunkPacket, layoutChunkCount, layoutEndPacket, layoutAbortPacket, layoutQueryPacket, layoutResetPacket, parseLayoutAck, uploadLayoutBytes };')();
const arr = (u8) => Array.from(u8);
const enc = (s) => new TextEncoder().encode(s);
const here = (p) => new URL(p, import.meta.url);

// CRC32 (zlib) known value + the golden value for the shared example, computed with python zlib.crc32
assert.equal(lib.crc32(enc('123456789')), 0xCBF43926);
assert.equal(lib.crc32(new Uint8Array(0)), 0);
const example = readFileSync(here('../docs/layout-example.json'), 'utf8').trim();
const exampleBytes = enc(example);
assert.equal(lib.crc32(exampleBytes), Number(readFileSync(here('../sketch/test/host/layout-example.crc'), 'utf8').trim()), 'crc of docs/layout-example.json');
const v = lib.validateLayout(example);
assert.equal(v.ok, true, 'docs/layout-example.json is valid: ' + v.err);
assert.ok(exampleBytes.length < 4096, 'example under the 4 KB target (' + exampleBytes.length + ' bytes)');

// shared cases: "<error name>\t<json on one line>"
let n = 0;
for (const line of readFileSync(here('../sketch/test/host/layout-cases.tsv'), 'utf8').split('\n')) {
  if (!line || line.startsWith('#')) continue;
  const tab = line.indexOf('\t');
  const want = line.slice(0, tab), text = line.slice(tab + 1).replace(/\\n/g, '\n');
  const got = lib.validateLayout(text);
  assert.equal(got.err, want, 'case: ' + text.slice(0, 80) + '  -> got "' + got.err + '", want "' + want + '"');
  n++;
}
assert.ok(n >= 60, 'cases loaded: ' + n);
assert.equal(lib.validateLayout('x'.repeat(4097)).err, 'too big');
assert.equal(lib.validateLayout('').err, 'empty');
assert.equal(lib.validateLayout('{"v":1,"pg":[{"w":[{"t":"tx","x":0,"y":0,"w":10,"h":10,"s":"\ud800"}]}]}').err, 'syntax error', 'lone surrogate would be sent as \\u escape, which the board refuses');
assert.equal(lib.validateLayout('{"v":1,"pg":[{"w":[{"t":"tx","x":0,"y":0,"w":10,"h":10,"s":"héllo wörld"}]}]}').ok, true, 'UTF-8 text is fine');
assert.equal(lib.validateLayout('{"v":1,"pg":[{"w":[{"t":"tx","x":0,"y":0,"w":10,"h":10,"s":"éééééééééééé"}]}]}').ok, true, '12 x 2 bytes = 24 bytes');
assert.equal(lib.validateLayout('{"v":1,"pg":[{"w":[{"t":"tx","x":0,"y":0,"w":10,"h":10,"s":"ééééééééééééa"}]}]}').err, 'bad text', '25 bytes');

// upload packets
const small = enc('{"v":1,"pg":[{}]}');
const begin = lib.layoutBeginPacket(small);
const crc = lib.crc32(small);
assert.deepEqual(arr(begin), [0xFF, 0x0B, 11, 0, 0, 1, small.length, 0, 0, 0, crc & 0xFF, (crc >>> 8) & 0xFF, (crc >>> 16) & 0xFF, crc >>> 24]);
assert.deepEqual(arr(lib.layoutChunkPacket(0, small)), [0xFF, 0x0C, 2 + small.length, 0, 0, ...small]);
assert.deepEqual(arr(lib.layoutEndPacket()), [0xFF, 0x0D, 0]);
assert.deepEqual(arr(lib.layoutAbortPacket()), [0xFF, 0x0E, 0]);
assert.deepEqual(arr(lib.layoutQueryPacket()), [0xFF, 0x0F, 0]);
assert.deepEqual(arr(lib.layoutResetPacket()), [0xFF, 0x11, 0]);
const bigBytes = new Uint8Array(1000).map((_, i) => i & 0xFF);
assert.equal(lib.layoutChunkCount(1000), 6);
assert.equal(lib.layoutChunkCount(192), 1);
assert.equal(lib.layoutChunkCount(193), 2);
const c2 = lib.layoutChunkPacket(2, bigBytes);
assert.equal(c2.length, 3 + 2 + 192); assert.equal(c2[2], 194); assert.equal(c2[3], 2); assert.equal(c2[4], 0); assert.equal(c2[5], bigBytes[384]);
const c5 = lib.layoutChunkPacket(5, bigBytes);
assert.equal(c5[2], 2 + (1000 - 960)); assert.equal(c5.length, 3 + 2 + 40);
for (let i = 0; i < 6; i++) assert.ok(lib.layoutChunkPacket(i, bigBytes).length <= 256, 'fits the firmware packet buffer');
assert.ok(lib.layoutChunkPacket(300, new Uint8Array(300 * 192 + 5))[3] === (300 & 0xFF) && lib.layoutChunkPacket(300, new Uint8Array(300 * 192 + 5))[4] === 1, 'seq is 16 bit little endian');

// ack decoding (bytes as produced by ack_encode in layout_core.cpp)
const ackBytes = Uint8Array.from([0xFF, 0x10, 12, 0x0D, 8, 3, 0x05, 0x02, 0x00, 0xC3, 0x00, 0x78, 0x56, 0x34, 0x12]);
const ack = lib.parseLayoutAck(ackBytes);
assert.deepEqual({ cmd: ack.cmd, status: ack.status, detail: ack.detail, seq: ack.seq, freeKb: ack.freeKb, crc: ack.crc }, { cmd: 0x0D, status: 8, detail: 3, seq: 2, freeKb: 195, crc: 0x12345678 });
assert.equal(ack.statusName, 'invalid layout'); assert.equal(ack.detailName, 'syntax error');
assert.equal(ack.active, true); assert.equal(ack.safeMode, false); assert.equal(ack.sessionOpen, true); assert.equal(ack.stored, false);
assert.equal(lib.parseLayoutAck(ackBytes.subarray(0, 14)), null);
assert.equal(lib.parseLayoutAck(Uint8Array.from([0xFF, 0x05, 12, ...new Array(12).fill(0)])), null);

// upload driver against a small simulated board (retry, resume, lost ack, interrupted link)
function makeBoard(opts = {}) {
  const b = { open: false, total: 0, crc: 0, data: [], stored: null, storedCrc: 0, sent: [], dropAcks: opts.dropAcks || 0, rejectCrc: !!opts.rejectCrc };
  const mk = (cmd, status, detail = 0) => lib.parseLayoutAck(Uint8Array.from([0xFF, 0x10, 12, cmd, status, detail, 0, Math.ceil(b.data.length / 192), 0, 100, 0, b.storedCrc & 255, (b.storedCrc >>> 8) & 255, (b.storedCrc >>> 16) & 255, b.storedCrc >>> 24]));
  b.handle = (u) => {
    b.sent.push(u[1]);
    const id = u[1], p = Array.from(u.subarray(3));
    if (id === 0x0B) {
      const total = p[3] | (p[4] << 8) | (p[5] << 16), crc = (p[7] | (p[8] << 8) | (p[9] << 16) | (p[10] << 24)) >>> 0;
      if (!(b.open && b.total === total && b.crc === crc)) { b.open = true; b.total = total; b.crc = crc; b.data = []; }
      return mk(id, 0);
    }
    if (id === 0x0C) {
      const seq = p[0] | (p[1] << 8), d = p.slice(2), exp = Math.ceil(b.data.length / 192);
      if (!b.open) return mk(id, 9);
      if (exp > 0 && seq === exp - 1) return mk(id, 0);
      if (seq !== exp) return mk(id, 2);
      b.data.push(...d);
      return mk(id, 0);
    }
    if (id === 0x0D) {
      if (!b.open) return mk(id, 9);
      if (b.data.length !== b.total) return mk(id, 10);
      if (b.rejectCrc || lib.crc32(Uint8Array.from(b.data)) !== b.crc) { b.open = false; return mk(id, 4); }
      b.stored = b.data.slice(); b.storedCrc = b.crc; b.open = false; return mk(id, 0);
    }
    if (id === 0x0E) { b.open = false; b.data = []; return mk(id, 0); }
    if (id === 0x0F) return mk(id, 0);
    return mk(id, 1);
  };
  b.io = {
    send: (u) => { b.last = b.handle(u); },
    ack: async (cmd) => { if (b.dropAcks > 0) { b.dropAcks--; return null; } return b.last && b.last.cmd === cmd ? b.last : null; },
  };
  return b;
}
{
  const data = enc(example.padEnd(1000, ' ')); // multi-chunk
  const b = makeBoard();
  const progress = [];
  const r = await lib.uploadLayoutBytes(data, { ...b.io, onProgress: (d, t) => progress.push(d + '/' + t) });
  assert.equal(r.ok, true, r.message); assert.deepEqual(b.stored, Array.from(data)); assert.equal(progress.at(-1), '6/6');
  // lost acks: every chunk is retried, duplicates are harmless
  const b2 = makeBoard({ dropAcks: 2 });
  const r2 = await lib.uploadLayoutBytes(data, b2.io);
  assert.equal(r2.ok, true, r2.message); assert.deepEqual(b2.stored, Array.from(data));
  // link dead: gives up with an explicit message and sends abort
  const b3 = makeBoard({ dropAcks: 1000 });
  const r3 = await lib.uploadLayoutBytes(data, b3.io, { tries: 2 });
  assert.equal(r3.ok, false); assert.match(r3.message, /no answer/);
  // interrupted upload, then resumed by a new call: only the missing chunks are sent
  const b4 = makeBoard();
  b4.handle(lib.layoutBeginPacket(data)); b4.handle(lib.layoutChunkPacket(0, data)); b4.handle(lib.layoutChunkPacket(1, data));
  b4.sent.length = 0;
  const r4 = await lib.uploadLayoutBytes(data, b4.io);
  assert.equal(r4.ok, true, r4.message); assert.equal(b4.sent.filter((x) => x === 0x0C).length, 4, 'chunks 2..5 only'); assert.deepEqual(b4.stored, Array.from(data));
  // board reports a checksum error
  const r5 = await lib.uploadLayoutBytes(data, makeBoard({ rejectCrc: true }).io);
  assert.equal(r5.ok, false); assert.match(r5.message, /checksum mismatch/);
}
console.log('layout tests ok (' + n + ' shared cases)');
