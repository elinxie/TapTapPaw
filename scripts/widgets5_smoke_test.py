#!/usr/bin/env python3
"""Timer button, note and next-event widget tests (feature level 5) against the REAL board. Run on its own with
    ~/.venvs/pio39/bin/python -u scripts/widgets5_smoke_test.py [--port P]
or as step 19 of layout_smoke_test.py. Ends with a stored layout of its own: the full test resets it. Needs the TapTapPaw desktop app quit."""
import argparse, os, re, sys, time, zlib
sys.path.insert(0, os.path.dirname(__file__))
from layout_tool import Board


def run(b, check, full):
    # 19 timer button, note and next event (feature level 5): on screen with the clock running, pages cycling, long text, refused variants
    q = b.query()
    check("firmware reports feature level 5 (timer button, note, next event)", q is not None and q.detail >= 5, q and q.detail)
    note100 = "n" * 100
    w5 = ('{"v":1,"rot":3,"pg":[{"bg":{"t":"s","c":"#101820"},"w":['
          '{"t":"tm","x":8,"y":8,"w":104,"h":52,"p":5},{"t":"tm","x":124,"y":8,"w":104,"h":52,"p":25,"s":"Focus","z":1,"r":14},'
          '{"t":"nt","x":8,"y":76,"w":224,"h":80,"s":"%s","z":0,"a":1},{"t":"ne","x":8,"y":170,"w":224,"h":50,"s":"09:30 Standup|13:00 Lunch|23:59 Bed"}]},'
          '{"w":[{"t":"ne","x":8,"y":10,"w":224,"h":50,"s":"00:01 Nothing left today"},{"t":"nt","x":8,"y":80,"w":224,"h":60,"s":"Be kind. To yourself.","a":1,"z":1},'
          '{"t":"tm","x":60,"y":160,"w":120,"h":60,"p":1,"s":"Egg"}]}]}') % note100
    w5 = w5.encode()
    ok, msg = full(b, w5)
    check("layout with timer buttons, a 100 byte note and next-event widgets uploads (%d bytes)" % len(w5), ok, msg)
    crc5 = zlib.crc32(w5)
    t_end = time.time() + 26.0
    log = ""
    while time.time() < t_end:  # the clock the next-event widget reads comes in the status packet
        h, m, s_ = time.localtime()[3:6]
        b.send(bytes([0xFF, 0x01, 9, 0x00, 20, 30, 80, 0, h, m, s_, 1]))
        log += b.read_text(1.0)
    pairs = [(int(m.group(1)), int(m.group(2))) for m in re.finditer(r"layout page (\d+)/\d+ heap free (\d+)", log)]
    check("no crash or reset while the new widgets ran", "Guru Meditation" not in log and "abort()" not in log and "rst:" not in log, log[-300:])
    q = b.query()
    check("board alive after the new widgets ran", q is not None and (q.flags & 1) and q.crc == crc5, q)
    for name, data, why in (
        ("note of 101 bytes", w5.replace(note100.encode(), (note100 + "n").encode(), 1), "bad text"),
        ("timer button without minutes", w5.replace(b'"p":5}', b'"s":"x"}', 1), "value out of range"),
        ("timer button of 181 minutes", w5.replace(b'"p":25', b'"p":181', 1), "value out of range"),
        ("next event without text", w5.replace(b',"s":"09:30 Standup|13:00 Lunch|23:59 Bed"', b'', 1), "bad text"),
        ("a 25 byte label on a timer button", w5.replace(b'"s":"Focus"', b'"s":"' + b"t" * 25 + b'"', 1), "bad text"),
    ):
        ok, msg = full(b, data)
        check("refused: " + name, (not ok) and why in msg and b.query().crc == crc5, msg)
    # heap: pages built a few times by rotation, then left alone
    if len(pairs) >= 4:
        drift = min(pairs[i][1] for i in range(min(3, len(pairs)))) - min(p[1] for p in pairs[-3:])
        check("timer/note/event widgets: heap flat across page changes (worst drift %d B, min %d B)" % (drift, min(p[1] for p in pairs)), drift <= 2048 and min(p[1] for p in pairs) > 40000, pairs)
    else:
        check("timer/note/event widgets: pages cycled (%d changes)" % len(pairs), len(pairs) >= 4, pairs)


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
    b.reset_layout()
    b.close()
    print("\n%s" % ("ALL PASSED" if not fails[0] else "%d FAILED" % fails[0]))
    sys.exit(1 if fails[0] else 0)


if __name__ == "__main__":
    main()
