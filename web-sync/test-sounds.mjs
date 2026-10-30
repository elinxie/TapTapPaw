// Tests for the custom-sound code in index.html: WAV encode/check (must agree with wav_check in layout_core.cpp), mono mixing,
// resampling, cut/trim/normalize/fade, free-space maths, packets, and the upload driver against a simulated board.
// Run: node web-sync/test-sounds.mjs
import { readFileSync } from 'node:fs';
import assert from 'node:assert/strict';

const html = readFileSync(new URL('./index.html', import.meta.url), 'utf8');
const code = html.split('<script>')[1].split('// ---- UI ----')[0];
const lib = new Function(code + '; return { pkt, crc32, wavEncode, wavCheck, mixToMono, resample, prepareSound, soundBytesFit, soundFitSeconds, layoutQueryPacket, layoutResetPacket, layoutBeginPacket, playPacket, uploadLayoutBytes, parseLayoutAck, validateLayout, SND, SOUND_SLOTS };')();
const { wavEncode, wavCheck, mixToMono, resample, prepareSound, SND } = lib;
const arr = (u8) => Array.from(u8);
const sine = (n, rate, hz = 440, amp = 0.5) => Float32Array.from({ length: n }, (_, i) => amp * Math.sin(2 * Math.PI * hz * i / rate));

// ---- wavEncode / wavCheck
const pcm = Int16Array.from({ length: 1600 }, (_, i) => Math.round(8000 * Math.sin(i / 5)));
const w = wavEncode(pcm, 16000);
assert.equal(w.length, 44 + 3200);
assert.equal(new TextDecoder().decode(w.subarray(0, 4)), 'RIFF');
assert.equal(new DataView(w.buffer).getUint32(4, true), w.length - 8);
let c = wavCheck(w);
assert.ok(c.ok && c.rate === 16000 && c.samples === 1600 && Math.abs(c.seconds - 0.1) < 1e-9, 'canonical WAV is accepted');
// the board and the page must refuse the same things: mutate every header field, one at a time (same table as test_wav_check in layout_core_test.cpp)
const muts = [[0, 0x58], [8, 0x58], [12, 0x58], [16, 18], [20, 3], [20, 0], [22, 2], [34, 8], [32, 4], [36, 0x58], [4, 0], [40, 0], [28, 1]];
for (const [off, val] of muts) { const b = Uint8Array.from(w); b[off] = val; assert.equal(wavCheck(b).ok, false, 'header byte ' + off + ' = ' + val + ' must be refused'); }
for (const rate of [7999, 24001]) assert.equal(wavCheck(wavEncode(pcm, rate)).ok, false, 'rate ' + rate);
for (const rate of [8000, 11025, 16000, 22050, 24000]) assert.equal(wavCheck(wavEncode(pcm, rate)).ok, true, 'rate ' + rate);
assert.equal(wavCheck(w.subarray(0, w.length - 1)).ok, false, 'half a sample');
assert.equal(wavCheck(w.subarray(0, 43)).ok, false);
assert.equal(wavCheck(null).ok, false);
assert.equal(wavCheck(new Uint8Array(0)).ok, false);
// exactly 5 s at 16 kHz is the limit
assert.equal(SND.MAX_BYTES, 160044);
assert.ok(wavCheck(wavEncode(new Int16Array(80000), 16000)).ok);
assert.equal(wavCheck(wavEncode(new Int16Array(80001), 16000)).ok, false);
assert.equal(wavCheck(wavEncode(new Int16Array(0), 16000)).ok, false, 'no samples');

// ---- mixToMono
const L = Float32Array.from([1, 0, -1]), R = Float32Array.from([0, 1, -1]);
assert.deepEqual(arr(mixToMono([L, R])), [0.5, 0.5, -1]);
assert.deepEqual(arr(mixToMono([L])), arr(L));

// ---- resample: length, identity, level and frequency kept
assert.equal(resample(sine(100, 16000), 16000, 16000).length, 100);
const down = resample(sine(48000, 48000, 440), 48000, 16000);
assert.equal(down.length, 16000);
let peak = 0; for (const v of down) peak = Math.max(peak, Math.abs(v));
assert.ok(peak > 0.45 && peak < 0.52, 'a 440 Hz sine keeps its level when 48k -> 16k (peak ' + peak.toFixed(3) + ')');
let cross = 0; for (let i = 1; i < down.length; i++) if (down[i - 1] < 0 && down[i] >= 0) cross++;
assert.ok(Math.abs(cross - 440) <= 2, 'and its pitch (' + cross + ' cycles in 1 s)');
const up = resample(sine(8000, 8000, 200), 8000, 16000);
assert.equal(up.length, 16000);
const hiAlias = resample(sine(48000, 48000, 7000, 0.5), 48000, 8000); // 7 kHz is above the 4 kHz Nyquist of 8 kHz: the box filter must tame it
let hp = 0; for (const v of hiAlias) hp = Math.max(hp, Math.abs(v));
assert.ok(hp < 0.5, 'a tone above Nyquist is not louder after downsampling');
assert.equal(resample(new Float32Array(0), 48000, 16000).length, 0);

// ---- prepareSound: slice, cap at 5 s, trim, normalize, fades
const rate = 16000;
let r = prepareSound(sine(rate * 10, rate), rate, {});
assert.equal(r.pcm.length, rate * 5, 'never more than 5 seconds');
r = prepareSound(sine(rate * 10, rate), rate, { start: 2, length: 1 });
assert.equal(r.pcm.length, rate, 'start and length');
r = prepareSound(sine(rate, rate), rate, { start: 0.5, length: 3 });
assert.equal(r.pcm.length, rate / 2, 'length is clipped at the end of the file');
const padded = new Float32Array(rate * 3); padded.set(sine(rate, rate, 440, 0.4), rate); // 1 s of silence, 1 s of sound, 1 s of silence
r = prepareSound(padded, rate, {});
assert.ok(Math.abs(r.seconds - 1.04) < 0.01, 'silence trimmed, 20 ms kept each side (' + r.seconds + ')');
r = prepareSound(padded, rate, { trim: false });
assert.equal(r.pcm.length, rate * 3);
r = prepareSound(sine(rate, rate, 440, 0.2), rate, { trim: false, fadeMs: 0 });
let mx = 0; for (const v of r.pcm) mx = Math.max(mx, Math.abs(v));
assert.ok(mx > 0.89 * 32767 && mx <= 0.91 * 32767, 'normalized to 90 % (' + mx + ')');
r = prepareSound(sine(rate, rate, 440, 0.05), rate, { trim: false, fadeMs: 0 }); // very quiet: gain stops at 8x, noise is not boosted to full scale
assert.equal(r.gain, 8); mx = 0; for (const v of r.pcm) mx = Math.max(mx, Math.abs(v));
assert.ok(mx > 0.39 * 32767 && mx < 0.41 * 32767);
r = prepareSound(sine(rate, rate, 440, 0.2), rate, { trim: false, fadeMs: 0 });
r = prepareSound(sine(rate, rate, 440, 0.1), rate, { normalize: false, trim: false, fadeMs: 0 });
mx = 0; for (const v of r.pcm) mx = Math.max(mx, Math.abs(v));
assert.ok(Math.abs(mx - 3277) < 5 && r.gain === 1, 'normalize off keeps the level');
r = prepareSound(sine(rate, rate, 440, 1.5), rate, { normalize: false, trim: false }); // over full scale: clipped, never wrapped around
assert.ok(r.pcm.every((v) => v >= -32768 && v <= 32767));
r = prepareSound(new Float32Array(rate).fill(0.5), rate, { trim: false, normalize: false, fadeMs: 10 });
assert.equal(r.pcm[0], 0); assert.equal(r.pcm[r.pcm.length - 1], 0); assert.ok(r.pcm[r.pcm.length >> 1] > 16000, 'fade in and out, full level in the middle');
r = prepareSound(new Float32Array(rate), rate, {});
assert.equal(r.pcm.length, 0, 'pure silence trims to nothing');
r = prepareSound(sine(rate, rate), rate, { start: 99 });
assert.equal(r.pcm.length, 0, 'start beyond the end');
const tiny = prepareSound(sine(10, rate), rate, { trim: false });
assert.equal(tiny.pcm.length, 10);

// ---- free space maths (the board keeps 8 KB spare)
assert.ok(lib.soundBytesFit(100 * 1024, 108) && !lib.soundBytesFit(100 * 1024 + 1, 108));
assert.equal(lib.soundFitSeconds(0, 16000), 0);
assert.ok(Math.abs(lib.soundFitSeconds(150, 16000) - (142 * 1024 - 44) / 32000) < 1e-9);
assert.equal(lib.soundFitSeconds(190, 16000), 5, '190 KB free holds the full 5 s');
assert.equal(lib.soundFitSeconds(500, 16000), 5, 'never above 5 s');
assert.ok(lib.soundFitSeconds(100, 8000) > lib.soundFitSeconds(100, 16000));

// ---- packets
assert.deepEqual(arr(lib.layoutQueryPacket(7, 3)), [0xFF, 0x0F, 2, 3, 7]);
assert.deepEqual(arr(lib.layoutQueryPacket(7)), [0xFF, 0x0F, 1, 7]); // a lone slot is still a picture
assert.deepEqual(arr(lib.layoutQueryPacket()), [0xFF, 0x0F, 0]);
assert.deepEqual(arr(lib.layoutResetPacket(14, 3)), [0xFF, 0x11, 2, 3, 14]);
assert.deepEqual(arr(lib.layoutResetPacket(2)), [0xFF, 0x11, 1, 2]);
assert.deepEqual(arr(lib.playPacket(9)), [0xFF, 0x14, 1, 9]);
const begin = lib.layoutBeginPacket(w, 3, 8);
assert.deepEqual(arr(begin.subarray(0, 6)), [0xFF, 0x0B, 11, 3, 8, 1]);
assert.equal(new DataView(begin.buffer).getUint32(6, true), w.length);
assert.equal(lib.SOUND_SLOTS.length, SND.SLOTS);
assert.ok(lib.SOUND_SLOTS.slice(0, SND.ORIGINALS).every((s) => /\.wav$/.test(s.original)) && lib.SOUND_SLOTS.slice(SND.ORIGINALS).every((s) => !s.original && s.fallback));

// ---- upload driver against a simulated board (kind 3), including a lost ack and a refused file
function fakeBoard(opts = {}) {
  const b = { open: false, data: [], total: 0, crc: 0, slot: 0, snds: {}, log: [], dropAck: opts.dropAck || 0 };
  const ack = (cmd, status, detail = 0, extra = {}) => Object.assign({ cmd, status, statusName: ['ok', 'bad packet', 'bad sequence', 'too big', 'checksum mismatch', 'unsupported', 'no space', 'flash write error', 'invalid layout'][status], detail, detailName: '', flags: 0, seq: Math.ceil(b.data.length / 192), freeKb: 180, crc: 0 }, extra);
  const io = {
    pending: null,
    send(u) {
      const id = u[1], p = Array.from(u.subarray(3)); b.log.push(id);
      let a;
      if (id === 0x0B) { b.slot = p[1]; b.total = p[3] | (p[4] << 8) | (p[5] << 16); b.crc = (p[7] | (p[8] << 8) | (p[9] << 16) | (p[10] << 24)) >>> 0; b.open = true; b.data = []; a = ack(id, 0); }
      else if (id === 0x0C) { const seq = p[0] | (p[1] << 8); if (seq !== Math.ceil(b.data.length / 192)) a = ack(id, 2); else { b.data.push(...p.slice(2)); a = ack(id, 0); } if (b.dropAck > 0 && seq === 3) { b.dropAck--; a = null; } }
      else if (id === 0x0D) { const bytes = Uint8Array.from(b.data); if (!lib.wavCheck(bytes).ok) a = ack(id, 8, 0); else { b.snds[b.slot] = bytes; a = ack(id, 0, 0, { crc: lib.crc32(bytes) }); } b.open = false; }
      else a = ack(id, 0);
      io.pending = a ? Object.assign(a, { cmd: id }) : null;
    },
    async ack(cmd) { const a = io.pending; io.pending = null; return a && a.cmd === cmd ? a : null; },
  };
  return { b, io };
}
{
  const { b, io } = fakeBoard();
  const big = wavEncode(Int16Array.from({ length: 16000 }, (_, i) => Math.round(5000 * Math.sin(i / 7))), 16000);
  let prog = 0;
  const res = await lib.uploadLayoutBytes(big, Object.assign(io, { onProgress: () => prog++ }), { kind: 3, slot: 8, timeoutMs: 20, tries: 3 });
  assert.ok(res.ok, res.message); assert.match(res.message, /sound stored and verified/);
  assert.deepEqual(arr(b.snds[8]), arr(big)); assert.ok(prog > 100, 'progress reported per chunk');
}
{
  const { b, io } = fakeBoard({ dropAck: 1 }); // the ack of chunk 3 is lost once: the driver resends, the board does not store it twice
  const wv = wavEncode(Int16Array.from({ length: 4000 }, (_, i) => Math.round(3000 * Math.sin(i / 3))), 16000);
  const res = await lib.uploadLayoutBytes(wv, io, { kind: 3, slot: 11, timeoutMs: 20, tries: 3 });
  assert.ok(res.ok, res.message); assert.deepEqual(arr(b.snds[11]), arr(wv));
}
{
  const { b, io } = fakeBoard();
  const junk = new Uint8Array(1000).fill(65); // not a WAV: the board refuses it at the end and says so
  const res = await lib.uploadLayoutBytes(junk, io, { kind: 3, slot: 0, timeoutMs: 20, tries: 3 });
  assert.equal(res.ok, false); assert.match(res.message, /refused this sound/); assert.equal(b.snds[0], undefined);
}
console.log('sound tests ok');
