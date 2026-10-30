#!/usr/bin/env python3
"""GIF widget tests (upload kind 4, query/reset [4, slot]) against the REAL board. Run on its own with
    ~/.venvs/pio39/bin/python -u scripts/gif_smoke_test.py [--port P]
or as step 18 of layout_smoke_test.py. Uses GIF slots 0..3 and removes what it creates; it SKIPS itself (nothing deleted) when a GIF slot
already holds a GIF. Needs the TapTapPaw desktop app quit."""
import argparse, os, re, struct, sys, time, zlib
sys.path.insert(0, os.path.dirname(__file__))
from layout_tool import Board, ST, begin_pkt, chunk_pkt, pkt, CHUNK

FX = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "sketch", "test", "fixtures")


def run(b, check, full):
    q = b.query()
    check("firmware reports feature level 4 (GIF widget)", q is not None and q.detail >= 4, q and q.detail)

    def taken(s):
        a = b.gif_crc(s)
        return a is None or a.status != 0 or a.crc != 0
    busy = [s for s in range(4) if taken(s)]
    if busy:
        print("    note: GIF slot(s) %s already hold a GIF: the GIF tests are skipped so nothing of yours is deleted" % busy)
        return
    g96 = open(os.path.join(FX, "gif_96x96.gif"), "rb").read()
    g128 = open(os.path.join(FX, "gif_128x128.gif"), "rb").read()
    base = b.query().free_kb
    t0 = time.time()
    ok, a = b.upload_kind(g96, 4, 0, end_timeout=8.0)
    check("96x96 GIF (%d B) uploads, passes the board's decode test, crc read back (%.1f s)" % (len(g96), time.time() - t0), ok, a)
    qs = b.gif_crc(0)
    check("query [4, slot] reports the crc and the size in KB", qs is not None and qs.crc == zlib.crc32(g96) and qs.detail == (len(g96) + 1023) // 1024, qs)
    ok, a = b.upload_kind(g128, 4, 3, end_timeout=8.0)
    check("128x128 GIF (%d B) uploads into slot 3" % len(g128), ok, a)
    check("the GIFs took space (%d -> %d KB)" % (base, b.query().free_kb), b.query().free_kb < base)

    def bad(name, data, detail, slot=1):
        ok_, a_ = b.upload_kind(data, 4, slot, end_timeout=8.0)
        q_ = b.gif_crc(slot)
        check("GIF refused: " + name, (not ok_) and a_ is not None and a_.status == ST["invalid layout"] and a_.detail == detail and q_.crc == 0 and not (q_.flags & 4), (a_, q_))
    hdr = lambda magic, w, h: magic + struct.pack("<HH", w, h)
    bad("not a GIF", b"NOTAGIF" + bytes(range(200)) + b";", 0)
    bad("GIF90a magic", hdr(b"GIF90a", 64, 64) + bytes(100) + b";", 0)
    bad("canvas 129 px wide", hdr(b"GIF89a", 129, 64) + bytes(100) + b";", 0)
    bad("canvas 0 px high", hdr(b"GIF89a", 64, 0) + bytes(100) + b";", 0)
    bad("no GIF trailer", g96[:-1] + b"\x00", 0)
    bad("a real header but a body the decoder cannot read", hdr(b"GIF89a", 64, 64) + bytes(range(200)) + b";", 1)
    bad("a truncated GIF (the second half is missing, so no trailer)", g96[:len(g96) // 2], 0)
    a = b.call(begin_pkt(g96, kind=4, slot=4), 0x0B)
    check("GIF slot 4 does not exist (bad packet)", a and a.status == ST["bad packet"], a)
    a = b.call(begin_pkt(b"GIF89a", kind=4, slot=1), 0x0B)
    check("a 6 byte GIF is refused at BEGIN (bad packet)", a and a.status == ST["bad packet"], a)
    a = b.call(begin_pkt(b"x" * 10, kind=4, slot=1, total=65537), 0x0B)
    check("a GIF over 64 KB is refused at BEGIN (too big)", a and a.status == ST["too big"], a)
    for pk, nm in ((pkt(0x0F, bytes([4, 4])), "query"), (pkt(0x11, bytes([4, 4])), "reset")):
        a = b.call(pk, pk[1])
        check("%s of GIF slot 4 is a bad packet" % nm, a and a.status == ST["bad packet"], a)
    a = b.call(begin_pkt(g96, kind=4, slot=1, crc=0x12345678), 0x0B)
    for s in range((len(g96) + CHUNK - 1) // CHUNK):
        b.call(chunk_pkt(g96, s), 0x0C)
    a = b.call(pkt(0x0D), 0x0D, timeout=8.0)
    check("GIF with a wrong crc refused (checksum mismatch), slot stays empty", a and a.status == ST["checksum mismatch"] and b.gif_crc(1).crc == 0, a)
    n = (len(g96) + CHUNK - 1) // CHUNK
    b.call(begin_pkt(g96, kind=4, slot=1), 0x0B)
    for s in range(5):
        b.call(chunk_pkt(g96, s), 0x0C)
    a = b.call(begin_pkt(g96, kind=4, slot=1), 0x0B)
    check("repeated BEGIN resumes at chunk 5", a and a.status == 0 and a.seq == 5, a)
    for s in range(5, n):
        b.call(chunk_pkt(g96, s), 0x0C)
    a = b.call(pkt(0x0D), 0x0D, timeout=8.0)
    check("resumed GIF upload commits", a and a.status == 0 and b.gif_crc(1).crc == zlib.crc32(g96), a)
    b.call(begin_pkt(g128, kind=4, slot=2), 0x0B)
    for s in range(10):
        b.call(chunk_pkt(g128, s), 0x0C)
    free_cut = b.query().free_kb
    b.hard_reset()
    q_ = b.gif_crc(2)
    check("after a reset mid-upload: no GIF in slot 2, no session, no temp file (free %d KB, was %d KB)" % (b.query().free_kb, free_cut), q_ and q_.crc == 0 and not (q_.flags & 4) and b.query().free_kb >= free_cut - 1, q_)
    a = b.remove_gif(1)
    check("remove GIF 1", a and a.status == 0 and b.gif_crc(1).crc == 0 and b.gif_crc(0).crc == zlib.crc32(g96) and b.gif_crc(3).crc == zlib.crc32(g128), a)

    # GIFs on screen: page 1 one GIF, page 2 two GIFs (96x96 and 128x128: the most that fit) and a picture background; slot 1 is empty (placeholder)
    lay = (b'{"v":1,"rot":3,"pg":[{"bg":{"t":"s","c":"#101820"},"w":[{"t":"gf","x":72,"y":20,"w":96,"h":96,"i":0},{"t":"gf","x":8,"y":150,"w":100,"h":100,"i":1},{"t":"tx","x":0,"y":280,"w":240,"h":20,"a":1,"s":"GIFs"}]},'
           b'{"w":[{"t":"gf","x":56,"y":10,"w":128,"h":128,"i":3},{"t":"gf","x":72,"y":160,"w":96,"h":96,"i":0}]}]}')
    ok, msg = full(b, lay)
    check("layout with GIF widgets (one slot empty) uploads", ok, msg)
    log = b.read_text(26.0)
    gl = re.findall(r"gif slot (\d+): (\d+)x(\d+), heap free (\d+)", log)
    heaps = [int(m.group(2)) for m in re.finditer(r"layout page (\d+)/\d+ heap free (\d+)", log)]
    print("    GIFs started: %s" % [(int(s), "%sx%s" % (w, h), int(f)) for s, w, h, f in gl[:4]])
    check("GIFs were opened on the pages (%d starts, sizes %s)" % (len(gl), sorted(set("%sx%s" % (w, h) for _, w, h, _ in gl))), len(gl) >= 4 and {"96x96", "128x128"} <= set("%sx%s" % (w, h) for _, w, h, _ in gl), gl[:3])
    check("pages cycled (%d changes)" % len(heaps), len(heaps) >= 6, heaps)
    if len(heaps) >= 6:
        drift = min(heaps[:3]) - min(heaps[-3:])
        check("heap flat across page changes with GIFs (worst drift %d B)" % drift, drift <= 2048, heaps)
        check("heap healthy (> 25 KB free, min %d)" % min(heaps), min(heaps) > 25000, min(heaps))
    check("no crash or watchdog in the log", "Guru Meditation" not in log and "abort()" not in log and "rst:" not in log, log[-300:])
    ok, a = b.upload_kind(g96, 4, 0, end_timeout=8.0)
    log = b.read_text(6.0)
    check("a GIF replaced while it is on screen (layout redraws, no crash)", ok and "Guru Meditation" not in log and "rst:" not in log, (a, log[-200:]))
    q_ = b.query()
    check("board alive after the GIF tests", q_ is not None and (q_.flags & 1), q_)
    full(b, b'{"v":1,"pg":[{}]}')
    for s in range(4):
        b.remove_gif(s)
    end = b.query()
    check("GIFs removed: free space back to %d KB (was %d KB; the /gif folder keeps 8 KB)" % (end.free_kb, base), end.free_kb >= base - 12, end)
    ok, a = b.upload_kind(g96, 4, 2, end_timeout=8.0)
    mid = b.query().free_kb
    b.remove_gif(2)
    check("no space leaks (%d KB)" % end.free_kb, ok and mid < end.free_kb and b.query().free_kb == end.free_kb, (ok, mid, end.free_kb, b.query().free_kb))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port")
    args = ap.parse_args()
    fails = [0]

    def check(name, cond, extra=""):
        print(("PASS  " if cond else "FAIL  ") + name + (("  " + str(extra)) if extra and not cond else ""))
        if not cond:
            fails[0] += 1
    b = Board(args.port, boot_wait=3.0)
    run(b, check, lambda bb, d: bb.upload(d))
    b.close()
    print("\n%s" % ("ALL PASSED" if not fails[0] else "%d FAILED" % fails[0]))
    sys.exit(1 if fails[0] else 0)


if __name__ == "__main__":
    main()
