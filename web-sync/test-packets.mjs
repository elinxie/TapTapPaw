// Checks the packet builders in index.html against the byte layouts used by app/core/taptappaw.js.
// Run: node web-sync/test-packets.mjs
import { readFileSync } from 'node:fs';
import assert from 'node:assert/strict';

const html = readFileSync(new URL('./index.html', import.meta.url), 'utf8');
const code = html.split('<script>')[1].split('// ---- UI ----')[0];
const lib = new Function(code + '; return { pkt, statusPacket, weatherPacket, settingsPacket, moodPacket, sourcePacket, firmwareRequest, musicPacket, classify, SRC_WEB, keymapPacket, KEY_ACTIONS, KEY_SLOTS, DEFAULT_KEYMAP, b64url, pkceChallenge, spotifyInfo, wifiPacket, locationPacket };')();
const arr = (u8) => Array.from(u8);

// status: [FF 01 09 evt cpu mem batt chg h m s media]
assert.deepEqual(arr(lib.statusPacket({ evt: 0, cpu: 0, mem: 0, batt: 87, chg: true, h: 15, m: 45, s: 9, media: 1 })),
  [0xFF, 0x01, 9, 0, 0, 0, 87, 1, 15, 45, 9, 1]);

// weather: [FF 03 05 group temp humid h m], sub-zero temperature as two's complement
assert.deepEqual(arr(lib.weatherPacket({ group: 2, tempC: -3, humidity: 80, h: 15, m: 45 })), [0xFF, 0x03, 5, 2, 0xFD, 80, 15, 45]);

// music: dataLength = 1 + title + 1 + artist
assert.deepEqual(arr(lib.musicPacket('Hi', 'Yo')), [0xFF, 0x02, 6, 2, 0x48, 0x69, 2, 0x59, 0x6F]);
assert.deepEqual(arr(lib.musicPacket('', '')), [0xFF, 0x02, 2, 0, 0]);
const long = lib.musicPacket('é'.repeat(80), 'a'.repeat(300)); // 160 bytes of title -> clipped to 100, artist clipped to 100
assert.equal(long[3], 100); assert.ok(long.length <= 256, 'fits the firmware packet buffer');
assert.equal(long[2] + 3, long.length);
const cut = lib.musicPacket('é'.repeat(51), ''); assert.equal(cut[3], 100, 'multi-byte characters are not split');

// settings, mood, source, firmware request
assert.deepEqual(arr(lib.settingsPacket({ fahrenheit: true, invert: false })), [0xFF, 0x04, 1, 1]);
assert.deepEqual(arr(lib.settingsPacket({ fahrenheit: false, invert: true })), [0xFF, 0x04, 1, 2]);
assert.deepEqual(arr(lib.settingsPacket({ fahrenheit: true, invert: true })), [0xFF, 0x04, 1, 3]);
assert.deepEqual(arr(lib.moodPacket(2)), [0xFF, 0x06, 1, 2]);
assert.deepEqual(arr(lib.sourcePacket(lib.SRC_WEB)), [0xFF, 0x07, 1, 2]);
assert.deepEqual(arr(lib.firmwareRequest()), [0xFF, 0x00]);

// key map: six action ids; defaults match KeyAction numbering in sketch/src/keymap.h
assert.deepEqual(arr(lib.keymapPacket(lib.DEFAULT_KEYMAP)), [0xFF, 0x08, 6, 1, 2, 1, 3, 10, 11]);
assert.equal(lib.KEY_ACTIONS.length, 12, 'ACT_COUNT in keymap.h');
assert.equal(lib.KEY_ACTIONS[1], 'Paw tap'); assert.equal(lib.KEY_ACTIONS[10], 'Info toast'); assert.equal(lib.KEY_ACTIONS[11], 'Reset taps');
assert.equal(lib.KEY_SLOTS.length, 6);

// weather group mapping (same table as app/core/weather.js)
const cases = [[0, true, 0], [1, false, 5], [3, true, 1], [45, false, 5], [63, true, 2], [95, true, 3], [75, true, 4], [999, true, 1]];
for (const [code, day, want] of cases) assert.equal(lib.classify(code, day), want, `classify(${code}, ${day})`);

// WiFi credentials 0x09: [ssid_len][ssid][pass_len][pass]; len byte = 2 + ssid + pass. Empty ssid forgets.
assert.deepEqual(arr(lib.wifiPacket('ab', 'xyz')), [0xFF, 0x09, 7, 2, 0x61, 0x62, 3, 0x78, 0x79, 0x7A]);
assert.deepEqual(arr(lib.wifiPacket('', '')), [0xFF, 0x09, 2, 0, 0]);
const wl = lib.wifiPacket('s'.repeat(40), 'p'.repeat(90)); assert.equal(wl[3], 32); assert.equal(wl[4 + 32], 63); assert.equal(wl[2], 2 + 32 + 63);
// location 0x0A: degrees * 1e5 as int32 little-endian
assert.deepEqual(arr(lib.locationPacket(37.3713, -122.0296)), [0xFF, 0x0A, 8, ...[3737130, -12202960].flatMap((v) => { const b = Buffer.alloc(4); b.writeInt32LE(v); return [...b]; })]);

// Spotify: PKCE S256 test vector from RFC 7636 appendix B
assert.equal(await lib.pkceChallenge('dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk'), 'E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM');
assert.equal(lib.b64url(Uint8Array.from([0xfb, 0xff, 0xfe])), '-__-'); // url-safe alphabet, no padding

// Spotify: currently-playing answers. 204 (null) = nothing playing
assert.deepEqual(lib.spotifyInfo(null), { title: '', artist: '', state: 0 });
assert.deepEqual(lib.spotifyInfo({ is_playing: true, currently_playing_type: 'track', item: { name: 'Song', artists: [{ name: 'A' }, { name: 'B' }] } }), { title: 'Song', artist: 'A, B', state: 1 });
assert.deepEqual(lib.spotifyInfo({ is_playing: false, item: { name: 'Song', artists: [{ name: 'A' }] } }), { title: 'Song', artist: 'A', state: 2 });
assert.deepEqual(lib.spotifyInfo({ is_playing: true, currently_playing_type: 'episode', item: { name: 'Ep 1', show: { name: 'The Show' } } }), { title: 'Ep 1', artist: 'The Show', state: 1 });
assert.deepEqual(lib.spotifyInfo({ is_playing: true, currently_playing_type: 'ad', item: null }), { title: 'Advertisement', artist: '', state: 1 });

console.log('web-sync packet tests: all passed');
