#!/usr/bin/env python3
"""Smoke test of the layout upload path against the REAL board (pyserial). Run:
    ~/.venvs/pio39/bin/python scripts/layout_smoke_test.py [--port /dev/cu.usbserial-110]
Needs firmware with the layout engine flashed, the TapTapPaw desktop app quit, and no layout the user wants to keep
(the test ends with RESET, i.e. back to the built-in screens). Exit code 0 = every check passed."""
import argparse, os, re, struct, sys, time, zlib
sys.path.insert(0, os.path.dirname(__file__))
from layout_tool import Board, ST, begin_pkt, chunk_pkt, pkt, CHUNK
import sound_smoke_test
import gif_smoke_test
import widgets5_smoke_test

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
fails = 0


def check(name, cond, extra=""):
    global fails
    print(("PASS  " if cond else "FAIL  ") + name + (("  " + str(extra)) if extra and not cond else ""))
    if not cond:
        fails += 1


def full(b, data):
    return b.upload(data)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port")
    args = ap.parse_args()
    example = open(os.path.join(ROOT, "docs/layout-example.json"), "rb").read().strip()
    crc_ex = zlib.crc32(example)

    b = Board(args.port, boot_wait=0.5)
    b.hard_reset(boot_wait=0)  # reboot and capture the boot log (opening the port does not always reset the board)
    boot = b.read_text(5.0)
    m = re.search(r"layout: display (\d+)x(\d+)", boot)
    check("display reported at boot is 240x320 portrait (the layout limits)", m is not None and (m.group(1), m.group(2)) == ("240", "320"), m.group(0) if m else "no display line in the boot log")
    q = b.query()
    check("board answers a query", q is not None)
    if q is None:
        sys.exit("no ack from the board: wrong firmware, port busy, or board not booted")
    print("start state:", q)
    b.reset_layout()  # known start

    # 1 valid upload, verify crc read back
    ok, msg = full(b, example)
    check("valid layout uploads and verifies (%d bytes)" % len(example), ok, msg)
    q = b.query()
    check("query reports the stored crc, layout active, stored flag", q and q.crc == crc_ex and (q.flags & 1) and (q.flags & 8), q)

    # 2 malformed layouts with valid framing + crc: rejected, previous layout untouched
    cases = {
        "truncated layout": example[:-7],
        "bad version inside json": example.replace(b'"v":1', b'"v":7', 1),
        "unknown widget": example.replace(b'"t":"dc"', b'"t":"zz"', 1),
        "widget off screen": example.replace(b'"x":12,"y":10,"w":170', b'"x":100,"y":10,"w":170', 1),
        "not json": b"hello board",
        "empty object": b"{}",
    }
    for name, data in cases.items():
        ok, msg = full(b, data)
        q = b.query()
        check("rejected: " + name, (not ok) and "invalid layout" in msg and q.crc == crc_ex, msg)

    # 3 oversized / bad framing
    a = b.call(begin_pkt(b"x" * 10, total=5000), 0x0B)
    check("oversized layout (5000 B) refused at BEGIN", a and a.status == ST["too big"], a)
    a = b.call(begin_pkt(b"x" * 10, kind=5), 0x0B)
    check("unknown upload kind refused", a and a.status == ST["bad packet"], a)
    a = b.call(chunk_pkt(example, 0), 0x0C)
    check("chunk without BEGIN refused", a and a.status == ST["no upload in progress"], a)
    a = b.call(pkt(0x0D), 0x0D)
    check("END without BEGIN refused", a and a.status == ST["no upload in progress"], a)

    # 4 wrong crc announced
    big = example + b" " * (CHUNK * 3 - len(example) % CHUNK)  # trailing spaces keep it valid JSON, several chunks
    n = (len(big) + CHUNK - 1) // CHUNK
    a = b.call(begin_pkt(big, crc=0x12345678), 0x0B)
    for s in range(n):
        b.call(chunk_pkt(big, s), 0x0C)
    a = b.call(pkt(0x0D), 0x0D)
    q = b.query()
    check("wrong crc refused at END, old layout kept", a and a.status == ST["checksum mismatch"] and q.crc == crc_ex and not (q.flags & 4), (a, q))

    # 5 out of order chunk + duplicate chunk
    b.call(begin_pkt(big), 0x0B)
    a = b.call(chunk_pkt(big, 1), 0x0C)
    check("out-of-order chunk answers bad sequence with the expected seq", a and a.status == ST["bad sequence"] and a.seq == 0, a)
    a = b.call(chunk_pkt(big, 0), 0x0C)
    a2 = b.call(chunk_pkt(big, 0), 0x0C)  # lost-ack retry
    check("duplicate of the last chunk is accepted", a and a2 and a2.status == 0 and a2.seq == 1, (a, a2))

    # 6 interrupted upload, resumed on the same connection
    a = b.call(chunk_pkt(big, 1), 0x0C)
    a = b.call(begin_pkt(big), 0x0B)  # host lost track and sends BEGIN again
    check("repeated BEGIN resumes at the next missing chunk", a and a.status == 0 and a.seq == 2 and (a.flags & 4), a)
    for s in range(2, n):
        a = b.call(chunk_pkt(big, s), 0x0C)
    a = b.call(pkt(0x0D), 0x0D)
    q = b.query()
    check("resumed upload commits", a and a.status == 0 and q.crc == zlib.crc32(big), (a, q))

    # 7 abort drops a partial upload, keeps the stored layout
    b.call(begin_pkt(example), 0x0B)
    b.call(chunk_pkt(example, 0), 0x0C)
    a = b.abort()
    q = b.query()
    check("abort leaves no session and keeps the stored layout", a and a.status == 0 and not (q.flags & 4) and q.crc == zlib.crc32(big), q)

    # 8 interrupted by closing the port (cable pulled / tab closed). The board keeps the partial upload open: the old layout is intact
    # and a new connection can resume where it stopped.
    big2 = example.replace(b"Desk companion", b"Other   companion") + b" " * (CHUNK * 2)
    n2 = (len(big2) + CHUNK - 1) // CHUNK
    b.call(begin_pkt(big2), 0x0B)
    b.call(chunk_pkt(big2, 0), 0x0C)
    b.close()
    time.sleep(1.0)
    b = Board(args.port, boot_wait=1.0)
    q = b.query()
    check("after closing the port mid-upload the stored layout is intact", q and q.crc == zlib.crc32(big), q)
    a = b.call(begin_pkt(big2), 0x0B)
    check("reconnect: BEGIN of the same layout resumes (seq 1) or restarts (seq 0)", a and a.status == 0 and a.seq in (0, 1), a)
    for s_ in range(a.seq if a else 0, n2):
        b.call(chunk_pkt(big2, s_), 0x0C)
    a = b.call(pkt(0x0D), 0x0D)
    q = b.query()
    check("resumed-after-reconnect upload commits", a and a.status == 0 and q.crc == zlib.crc32(big2), (a, q))

    # 8b power-loss style interruption: partial upload, then a real reboot (RTS reset). The partial file must not survive or corrupt anything.
    b.call(begin_pkt(big), 0x0B)
    b.call(chunk_pkt(big, 0), 0x0C)
    b.hard_reset()
    q = b.query()
    check("after a reset mid-upload: stored layout intact, no session, partial file removed",
          q and q.crc == zlib.crc32(big2) and not (q.flags & 4) and (q.flags & 1), q)
    ok, msg = full(b, example)
    check("a clean upload works after the reboot", ok, msg)

    # 9 garbage on the line does not break the parser
    b.send(bytes([0x00, 0x12, 0xFF, 0x0C, 0x02, 0x00]))  # truncated frame: the parser waits for its payload
    time.sleep(0.2)
    b.send(bytes(range(1, 40)))
    time.sleep(0.2)
    b.buf = b""
    b.ser.reset_input_buffer()
    q = b.query()
    check("line noise / truncated frame: board still answers after a resync", q is not None or b.query() is not None)

    # 11 every widget type on screen, cycling the pages (auto-rotate 3 s) while watching the heap in the firmware log
    import json
    lay = json.loads(example)
    lay["pg"] = [p for p in lay["pg"] if not p.get("m")]  # custom pages only, so page 0 is built from widgets right away
    lay["rot"] = 3
    cyc = json.dumps(lay, separators=(",", ":")).encode()
    ok, msg = full(b, cyc)
    check("layout with only custom pages (auto-rotate 3 s) uploads", ok, msg)
    log = b.read_text(40.0)
    heaps = [(int(m.group(1)), int(m.group(2))) for m in re.finditer(r"layout page (\d+)/\d+ heap free (\d+)", log)]
    pages = [h[0] for h in heaps]
    check("auto-rotate visited every page at least twice (%d page changes seen)" % len(heaps), len(heaps) >= 8 and set(pages) == {1, 2}, pages)
    if len(heaps) >= 8:
        first = [h[1] for h in heaps[:2]]
        last = [h[1] for h in heaps[-2:]]
        print("heap free after first page changes: %s, after the last ones: %s" % (first, last))
        check("heap does not leak across page changes (drop <= 2 KB)", min(first) - min(last) <= 2048, (first, last))
        check("heap stays healthy (> 40 KB free)", min(h[1] for h in heaps) > 40000, min(h[1] for h in heaps))
    q = b.query()
    check("board alive after cycling pages", q is not None and (q.flags & 1), q)

    # 12 typing cat (feature level 2): shown on a custom page while the "desktop app" types (status packets, keyDown bit)
    q = b.query()
    check("firmware reports feature level 2", q is not None and q.detail >= 2, q and q.detail)
    cat = b'{"v":1,"pg":[{"bg":{"t":"b","i":0},"w":[{"t":"dc","x":20,"y":20,"w":200,"h":40,"f":1,"z":1,"a":1},{"t":"ct","x":0,"y":180,"w":240,"h":132}]}]}'
    ok, msg = full(b, cat)
    check("layout with a typing cat uploads", ok, msg)
    t_end = time.time() + 4.0
    n_taps = 0
    while time.time() < t_end:
        h, m, s_ = time.localtime()[3:6]
        b.send(bytes([0xFF, 0x01, 9, 0x01, 20, 30, 80, 0, h, m, s_, 1]))  # status: evt bit0 = key down
        n_taps += 1
        time.sleep(0.15)
    time.sleep(0.5)
    b.buf = b""
    b.ser.reset_input_buffer()
    q = b.query()
    check("board alive after %d typing events with the cat on screen" % n_taps, q is not None and (q.flags & 1), q)

    # 13 every feature level 2 widget on screen (two pages, auto-rotate 3 s) + the date packet; heap must stay flat
    lv2 = (b'{"v":1,"rot":3,"pg":[{"bg":{"t":"g","c":"#102030","d":"#3050a0"},"w":['
           b'{"t":"pm","x":8,"y":8,"w":110,"h":110,"p":25,"b":5},{"t":"cd","x":126,"y":8,"w":106,"h":52,"p":1020,"s":"Home"},'
           b'{"t":"sw","x":126,"y":68,"w":106,"h":36},{"t":"dt","x":8,"y":126,"w":224,"h":22,"a":1},'
           b'{"t":"fs","x":8,"y":156,"w":224,"h":50,"s":"On a call"},{"t":"rm","x":8,"y":214,"w":224,"h":30,"k":1,"p":45},'
           b'{"t":"bt","x":8,"y":252,"w":72,"h":40,"e":1,"s":"Tap"},{"t":"sp","x":88,"y":252,"w":144,"h":40}]},'
           b'{"w":[{"t":"ct","x":0,"y":100,"w":240,"h":132},{"t":"bt","x":20,"y":20,"w":200,"h":44,"e":19,"g":2052,"s":"Cmd+A"}]}]}')
    ok, msg = full(b, lv2)
    check("layout with every new widget type uploads (%d bytes)" % len(lv2), ok, msg)
    b.send(bytes([0xFF, 0x12, 4, 26, 10, 29, 4]))  # date 2026-10-29, Thursday
    b.send(bytes([0xFF, 0x12, 4, 26, 13, 40, 9]))  # invalid date: ignored
    log = b.read_text(24.0)
    pairs = [(int(m.group(1)), int(m.group(2))) for m in re.finditer(r"layout page (\d+)/\d+ heap free (\d+)", log)]
    heaps = [h for _, h in pairs]
    check("new widgets: pages cycled (%d changes)" % len(heaps), len(heaps) >= 4, pairs)
    if len(heaps) >= 4:
        # heap is logged right after a page loads, before the page it replaced is freed, so readings alternate between two
        # levels; compare the lowest of the first three with the lowest of the last three
        drift = min(heaps[:3]) - min(heaps[-3:])
        check("new widgets: heap flat across page changes (worst drift %d B)" % drift, drift <= 2048, pairs)
        check("new widgets: heap healthy (> 40 KB)", min(heaps) > 40000, min(heaps))
    check("no crash or watchdog in the log", "Guru Meditation" not in log and "abort()" not in log and "rst:" not in log, log[-400:])
    q = b.query()
    check("board alive after the new widgets ran", q is not None and (q.flags & 1), q)

    # 14 pictures (kind 1, LittleFS) and album art (kind 2, RAM), decoded on the board before they are accepted
    fx = os.path.join(ROOT, "sketch", "test", "fixtures")
    big_pic = open(os.path.join(fx, "pic_240x320.sjpg"), "rb").read()
    small_pic = open(os.path.join(fx, "pic_96x64.sjpg"), "rb").read()
    prog_pic = open(os.path.join(fx, "pic_progressive.sjpg"), "rb").read()
    art = open(os.path.join(fx, "art_96.jpg"), "rb").read()
    free0 = b.query().free_kb
    t0 = time.time()
    ok, a = b.upload_kind(big_pic, 1, 0)
    dt = time.time() - t0
    check("full-screen picture (%d B) uploads, decodes on the board, crc read back (%.1f s, %.1f KB/s)" % (len(big_pic), dt, len(big_pic) / 1024 / max(dt, 0.01)), ok, a)
    q = b.picture_crc(0)
    check("query slot 0 reports the picture's crc", q is not None and q.crc == zlib.crc32(big_pic), q)
    ok, a = b.upload_kind(small_pic, 1, 1)
    check("small picture into slot 1", ok, a)
    ok, a = b.upload_kind(prog_pic, 1, 2)
    q = b.picture_crc(2)
    check("progressive JPEG is refused by the board's decode check and not kept", (not ok) and a is not None and a.status == ST["invalid layout"] and a.detail == 1 and q.crc == 0, (a, q))
    ok, a = b.upload_kind(art, 2, 0)
    check("album art (%d B, RAM) uploads and decodes" % len(art), ok, a)
    picl = (b'{"v":1,"pg":[{"bg":{"t":"i","i":0},"w":[{"t":"dc","x":20,"y":10,"w":200,"h":40,"f":1,"z":1,"a":1},'
            b'{"t":"pc","x":8,"y":200,"w":96,"h":64,"i":1},{"t":"aa","x":136,"y":184,"w":96,"h":96},{"t":"pc","x":8,"y":120,"w":96,"h":64,"i":5}]},'
            b'{"bg":{"t":"i","i":4},"w":[]}],"rot":3}')
    ok, msg = full(b, picl)
    check("layout with a picture background, picture widgets (one slot empty) and album art uploads", ok, msg)
    log = b.read_text(10.0)
    check("pages with pictures redraw without a crash (incl. an empty slot)", "Guru Meditation" not in log and "rst:" not in log, log[-300:])
    ok, a = b.upload_kind(art, 2, 0)  # replace the album art while it is on screen
    check("album art replaced while shown", ok, a)
    free1 = b.query().free_kb
    print("LittleFS free: %d KB before pictures, %d KB with two pictures stored" % (free0, free1))
    a0, a1 = b.remove_picture(0), b.remove_picture(1)
    q0, q1 = b.picture_crc(0), b.picture_crc(1)
    check("pictures removed", a0 and a0.status == 0 and a1 and a1.status == 0 and q0.crc == 0 and q1.crc == 0, (a0, a1, q0, q1))
    check("space returned after removing pictures", b.query().free_kb >= free0 - 4, b.query())
    q = b.query()
    check("board alive after the picture tests", q is not None and (q.flags & 1), q)

    # 15 cheerful waiting: after 60 s without activity the cat plays (wave, hearts, mouse) instead of sleeping. Runs for ~30 s here.
    ok, msg = full(b, cat)
    check("typing cat layout for the waiting test", ok, msg)
    log = b.read_text(92.0)
    marks = [k for k in ("Guru Meditation", "abort()", "rst:", "Backtrace", "assert failed", "watchdog") if k in log]
    if marks:  # keep the whole story of a reset: it is the only way to tell what happened
        i = max(0, min(log.find(k) for k in marks) - 800)
        open("/tmp/ttp_waiting_crash.log", "w").write(log)
        print("    reset markers %s; log around the first one (whole log in /tmp/ttp_waiting_crash.log):\n%s" % (marks, log[i:i + 2500]))
    check("cheerful waiting ran 30 s without a crash", not marks, marks)
    q = b.query()
    check("board alive after the waiting test", q is not None and (q.flags & 1), q)

    # 16 pets (claude-desktop-buddy species): one per page, the second a species the standard build does not carry (falls back to
    # the cat); the third page asks for two pets (41 KB of heap each) to show a failed allocation is skipped, not a crash
    q = b.query()
    print("    firmware carries all 18 pets: %s" % bool(q and q.flags & 64))
    pets = (b'{"v":1,"rot":3,"pg":[{"w":[{"t":"pt","x":40,"y":40,"w":160,"h":132,"n":4},{"t":"tx","x":0,"y":200,"w":240,"h":20,"a":1,"s":"cat"}]},'
            b'{"bg":{"t":"s","c":"#1b1620"},"w":[{"t":"pt","x":40,"y":120,"w":160,"h":132,"n":5}]},'
            b'{"w":[{"t":"pt","x":40,"y":0,"w":160,"h":132,"n":12},{"t":"pt","x":40,"y":160,"w":160,"h":132,"n":15}]}]}')
    ok, msg = full(b, pets)
    check("pet layout uploads (%d bytes)" % len(pets), ok, msg)
    log = b.read_text(24.0)
    heaps = [int(m.group(2)) for m in re.finditer(r"layout page (\d+)/\d+ heap free (\d+)", log)]
    check("pets: pages cycled (%d changes)" % len(heaps), len(heaps) >= 4, heaps)
    if len(heaps) >= 4:
        drift = min(heaps[:3]) - min(heaps[-3:])
        check("pets: heap flat across page changes (worst drift %d B)" % drift, drift <= 2048, heaps)
    check("pets: no crash", "Guru Meditation" not in log and "abort()" not in log and "rst:" not in log, log[-400:])
    q = b.query()
    check("board alive after the pets ran", q is not None and (q.flags & 1), q)

    # 17 custom sounds (kind 3): scripts/sound_smoke_test.py (also runnable on its own). Skips itself when you already keep custom sounds.
    sound_smoke_test.run(b, check, full)

    # 18 GIF widget (kind 4): scripts/gif_smoke_test.py (also runnable on its own). Skips itself when you already keep GIFs.
    gif_smoke_test.run(b, check, full)

    # 19 timer button, note and next event: scripts/widgets5_smoke_test.py (also runnable on its own)
    widgets5_smoke_test.run(b, check, full)

    # 10 reset
    a = b.reset_layout()
    q = b.query()
    check("reset deletes the layout", a and a.status == 0 and q.crc == 0 and not (q.flags & 8) and not (q.flags & 1), (a, q))
    print("end state:", q)
    b.close()
    print("\n%s" % ("ALL PASSED" if not fails else "%d FAILED" % fails))
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
