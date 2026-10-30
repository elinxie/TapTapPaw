#!/usr/bin/env python3
"""Custom sound tests (upload kind 3, query/reset [3, slot], PLAY 0x14) against the REAL board. Run on its own with
    ~/.venvs/pio39/bin/python -u scripts/sound_smoke_test.py [--port P]
or as step 17 of layout_smoke_test.py. Uses sound slots 0, 8, 9, 10 and 14 and removes what it creates; it SKIPS itself (nothing
deleted) when one of those slots already holds a custom sound. Needs the TapTapPaw desktop app quit."""
import argparse, os, re, struct, sys, time, zlib
sys.path.insert(0, os.path.dirname(__file__))
from layout_tool import Board, ST, begin_pkt, chunk_pkt, pkt, make_wav, CHUNK


def run(b, check, full):
    # custom sounds (kind 3): /snd/<slot>.wav next to the originals. Short, quiet test tones; every slot used here is cleaned up at the end.
    q = b.query()
    check("firmware reports feature level 3 (custom sounds)", q is not None and q.detail >= 3, q and q.detail)
    def slot_taken(s_):  # no answer counts as taken: better to skip than to delete something
        a_ = b.sound_crc(s_)
        return a_ is None or a_.status != 0 or a_.crc != 0
    taken = [s_ for s_ in (0, 8, 9, 10, 14) if slot_taken(s_)]
    if taken:
        print("    note: sound slot(s) %s already hold a custom sound: the sound tests are skipped so nothing of yours is deleted" % taken)
        return
    SND_POMO, SND_PET, SND_BTN4, SND_CONNECT = 9, 8, 14, 0
    base_free = b.query().free_kb
    qs = b.sound_crc(SND_POMO)
    check("an empty sound slot answers crc 0 and size 0", qs is not None and qs.status == 0 and qs.crc == 0 and qs.detail == 0, qs)
    wav1 = make_wav(16000)  # 1 s, 16 kHz
    t0 = time.time()
    ok, a = b.upload_kind(wav1, 3, SND_POMO, end_timeout=8.0)
    dt = time.time() - t0
    check("1 s sound (%d B) uploads, header validated, crc read back (%.1f s)" % (len(wav1), dt), ok, a)
    qs = b.sound_crc(SND_POMO)
    check("query [3, slot] reports the crc and the size in KB (32)", qs is not None and qs.crc == zlib.crc32(wav1) and qs.detail == 32, qs)
    free1 = b.query().free_kb
    check("the sound took about 32 KB of LittleFS (%d -> %d KB)" % (base_free, free1), 28 <= base_free - free1 <= 40, (base_free, free1))
    wav2 = make_wav(4000, rate=8000, hz=600)  # replace with 0.5 s at 8 kHz
    ok, a = b.upload_kind(wav2, 3, SND_POMO, end_timeout=8.0)
    qs = b.sound_crc(SND_POMO)
    check("a slot can be replaced (8 kHz accepted)", ok and qs.crc == zlib.crc32(wav2) and qs.detail == 8, (a, qs))
    check("replacing returned the space of the old copy", b.query().free_kb > free1, b.query())

    # malformed sounds are refused at END and the slot keeps its previous sound
    def bad(name, data):
        ok_, a_ = b.upload_kind(data, 3, SND_POMO, end_timeout=8.0)
        q_ = b.sound_crc(SND_POMO)
        check("sound refused: " + name, (not ok_) and a_ is not None and a_.status == ST["invalid layout"] and q_.crc == zlib.crc32(wav2) and not (q_.flags & 4), (a_, q_))
    good = bytearray(make_wav(3000))
    def mut(off, val):
        d = bytearray(good)
        d[off:off + len(val)] = val
        return bytes(d)
    bad("not RIFF", mut(0, b"RIFX"))
    bad("not WAVE", mut(8, b"WAVX"))
    bad("stereo", mut(22, struct.pack("<H", 2)))
    bad("8-bit", mut(34, struct.pack("<H", 8)))
    bad("float format", mut(20, struct.pack("<H", 3)))
    bad("sample rate 4000 Hz", mut(24, struct.pack("<II", 4000, 8000)))
    bad("sample rate 48000 Hz", mut(24, struct.pack("<II", 48000, 96000)))
    bad("RIFF size wrong", mut(4, struct.pack("<I", 999)))
    bad("data size wrong", mut(40, struct.pack("<I", 100)))
    bad("odd number of bytes", bytes(good) + b"\x00")
    bad("plain text", b"this is not a sound at all " * 40)
    bad("a JPEG", b"\xFF\xD8" + bytes(range(256)) * 8 + b"\xFF\xD9")

    # framing limits
    a = b.call(begin_pkt(wav1, kind=3, slot=15), 0x0B)
    check("sound slot 15 does not exist (bad packet)", a and a.status == ST["bad packet"], a)
    a = b.call(begin_pkt(b"RIFF" * 5, kind=3, slot=1), 0x0B)
    check("a 20 byte sound is refused at BEGIN (bad packet)", a and a.status == ST["bad packet"], a)
    a = b.call(begin_pkt(b"x" * 10, kind=3, slot=1, total=160045), 0x0B)
    check("a sound over 5 s (160,045 B) is refused at BEGIN (too big)", a and a.status == ST["too big"], a)
    a = b.call(pkt(0x0F, bytes([3, 15])), 0x0F)
    check("query of sound slot 15 is a bad packet", a and a.status == ST["bad packet"], a)
    a = b.call(pkt(0x11, bytes([3, 15])), 0x11)
    check("reset of sound slot 15 is a bad packet", a and a.status == ST["bad packet"], a)
    a = b.call(pkt(0x14, bytes([15])), 0x14)
    check("play of sound slot 15 is a bad packet", a and a.status == ST["bad packet"], a)
    a = b.call(pkt(0x14), 0x14)
    check("play without a slot is a bad packet", a and a.status == ST["bad packet"], a)

    # wrong crc at END
    wav3 = make_wav(3000, hz=500)
    a = b.call(begin_pkt(wav3, kind=3, slot=SND_POMO, crc=0x12345678), 0x0B)
    for s_ in range((len(wav3) + CHUNK - 1) // CHUNK):
        b.call(chunk_pkt(wav3, s_), 0x0C)
    a = b.call(pkt(0x0D), 0x0D, timeout=8.0)
    q_ = b.sound_crc(SND_POMO)
    check("sound with a wrong crc refused (checksum mismatch), old sound kept", a and a.status == ST["checksum mismatch"] and q_.crc == zlib.crc32(wav2), (a, q_))

    # interrupted: abort, resume, and a reset in the middle
    wav4 = make_wav(8000, hz=700)
    n4 = (len(wav4) + CHUNK - 1) // CHUNK
    b.call(begin_pkt(wav4, kind=3, slot=SND_POMO), 0x0B)
    for s_ in range(20):
        b.call(chunk_pkt(wav4, s_), 0x0C)
    a = b.abort()
    q_ = b.sound_crc(SND_POMO)
    check("abort mid-upload: old sound kept, no session left", a and a.status == 0 and q_.crc == zlib.crc32(wav2) and not (q_.flags & 4), (a, q_))
    b.call(begin_pkt(wav4, kind=3, slot=SND_POMO), 0x0B)
    for s_ in range(20):
        b.call(chunk_pkt(wav4, s_), 0x0C)
    a = b.call(begin_pkt(wav4, kind=3, slot=SND_POMO), 0x0B)
    check("repeated BEGIN of the same sound resumes at chunk 20", a and a.status == 0 and a.seq == 20, a)
    for s_ in range(20, n4):
        b.call(chunk_pkt(wav4, s_), 0x0C)
    a = b.call(pkt(0x0D), 0x0D, timeout=8.0)
    q_ = b.sound_crc(SND_POMO)
    check("resumed sound upload commits", a and a.status == 0 and q_.crc == zlib.crc32(wav4), (a, q_))
    free_before_cut = b.query().free_kb
    b.call(begin_pkt(wav3, kind=3, slot=SND_POMO), 0x0B)
    for s_ in range(10):
        b.call(chunk_pkt(wav3, s_), 0x0C)
    b.hard_reset()  # power-loss style
    q_ = b.sound_crc(SND_POMO)
    check("after a reset mid-upload the sound is intact and no partial file or session is left", q_ and q_.crc == zlib.crc32(wav4) and not (q_.flags & 4), q_)
    # the leftover temp file must be gone: the free space is what it was
    check("the cut-off upload left no temp file behind (free %d KB, was %d KB)" % (b.query().free_kb, free_before_cut), b.query().free_kb >= free_before_cut - 1, b.query())

    # originals are never touched: the firmware logs which file each slot plays ("sound slot 0 -> connect.wav" or "snd/0.wav");
    # the WAV reader's own line (data bytes) is compared as well when the log level shows it
    def played(slot):
        b.read_text(0.3)
        b.buf = b""
        b.send(pkt(0x14, bytes([slot])))  # the firmware logs the file before it acks, so collect the text first, then find the ack in it
        log_ = b.read_text(2.5)
        a_ = b.ack(0x14, 0.3)
        m_ = re.findall(r"sound slot %d -> (\S+)" % slot, log_)
        d_ = re.findall(r"data_bytes=(\d+)", log_)
        return a_, (m_[-1] if m_ else None), (int(d_[-1]) if d_ else None)
    a, path0, orig_bytes = played(SND_CONNECT)
    check("play slot 0 plays the original connect.wav (log: %s, %s data bytes)" % (path0, orig_bytes), a and a.status == 0 and path0 == "connect.wav", (a, path0))
    cw = make_wav(3000, hz=1000)  # 3000 samples: 6000 data bytes, different from the original
    ok, a = b.upload_kind(cw, 3, SND_CONNECT, end_timeout=8.0)
    check("custom 'connected' sound uploads", ok, a)
    a, path1, got = played(SND_CONNECT)
    check("slot 0 now plays the custom copy (log: %s, %s data bytes)" % (path1, got), path1 == "snd/0.wav" and (got is None or got == 6000), (path1, got))
    a = b.remove_sound(SND_CONNECT)
    q_ = b.sound_crc(SND_CONNECT)
    check("Reset sound deletes the custom copy (crc 0, size 0)", a and a.status == 0 and q_.crc == 0 and q_.detail == 0, (a, q_))
    a, path2, got2 = played(SND_CONNECT)
    check("after the reset slot 0 plays the original again (log: %s, %s data bytes, was %s)" % (path2, got2, orig_bytes), path2 == "connect.wav" and got2 == orig_bytes, (path2, got2, orig_bytes))
    a = b.remove_sound(SND_CONNECT)
    check("deleting a custom sound that is not there is fine", a and a.status == 0, a)
    a, path3, _ = played(SND_PET)
    check("a slot without an original and without a custom copy uses the built-in beep (log: %s)" % path3, path3 == "built-in", path3)

    # slots without an original play a built-in beep: play must not fail, and the board stays up
    for s_ in (SND_PET, 9, 10, 11):
        a = b.play(s_)
        check("play slot %d answers ok" % s_, a and a.status == 0, a)
        time.sleep(0.5)
    ok, a = b.upload_kind(make_wav(2000, hz=1500), 3, SND_PET, end_timeout=8.0)
    check("custom pet tap sound uploads", ok, a)
    a = b.play(SND_PET)
    time.sleep(0.6)
    check("play the custom pet tap sound", a and a.status == 0, a)

    # the largest sound (5 s, 160,044 B) and the space limit. Needs about 165 KB free: skipped (not failed) when pictures or sounds you
    # keep on the board use that space.
    b.remove_sound(SND_PET)
    b.remove_sound(SND_POMO)
    wmax = make_wav(80000, hz=300, amp=800)
    assert len(wmax) == 160044
    if b.query().free_kb < 172:
        print("    note: only %d KB free (a 5 s sound needs 172): the 5 s / out-of-space tests are skipped" % b.query().free_kb)
        ok, a = b.upload_kind(make_wav(1000, hz=900), 3, SND_BTN4, end_timeout=8.0)
        check("button sound 4 uploads", ok, a)
    else:
        t0 = time.time()
        ok, a = b.upload_kind(wmax, 3, 10, end_timeout=10.0)
        dt = time.time() - t0
        qs = b.sound_crc(10)
        check("a 5 s sound (160,044 B) uploads (%.0f s) and reads back" % dt, ok and qs.crc == zlib.crc32(wmax) and qs.detail == 157, (a, qs))
        fr = b.query().free_kb
        print("    LittleFS free with the 5 s sound stored: %d KB" % fr)
        a = b.call(begin_pkt(wmax, kind=3, slot=SND_BTN4), 0x0B)
        check("a second 5 s sound is refused before anything is written (no space), first one kept", a and a.status == ST["no space"] and b.sound_crc(10).crc == zlib.crc32(wmax), a)
        a = b.remove_sound(10)
        check("deleting the 5 s sound returns its space", a and a.status == 0 and b.query().free_kb >= fr + 140, (a, b.query()))
        ok, a = b.upload_kind(make_wav(1000, hz=900), 3, SND_BTN4, end_timeout=8.0)
        check("the freed space can be used again (button sound 4)", ok, a)


    # a layout with button press sounds: u 0..4 accepted, 5 refused, the stored layout stays
    btn = (b'{"v":1,"pg":[{"w":[{"t":"bt","x":8,"y":8,"w":104,"h":40,"e":1,"s":"A","u":1},{"t":"bt","x":120,"y":8,"w":104,"h":40,"e":2,"s":"B","u":4},'
           b'{"t":"bt","x":8,"y":60,"w":104,"h":40,"e":1,"s":"C"}]}]}')
    ok, msg = full(b, btn)
    check("layout with button press sounds (u = 1 and 4) uploads", ok, msg)
    crc_btn = zlib.crc32(btn)
    ok, msg = full(b, btn.replace(b'"u":4', b'"u":5'))
    check("button sound u = 5 is refused (value out of range), old layout kept", (not ok) and "value out of range" in msg and b.query().crc == crc_btn, msg)
    log = b.read_text(4.0)
    check("button layout shows without a crash", "Guru Meditation" not in log and "rst:" not in log, log[-300:])

    # clean up: every test sound removed, the space is back, other data untouched
    for s_ in (SND_CONNECT, SND_PET, SND_POMO, SND_BTN4, 10):
        b.remove_sound(s_)
    endq = b.query()
    # the first sound creates the /snd folder (a LittleFS directory is a 2-block metadata pair = 8 KB that stays when its files go), and
    # the test's last layout is bigger than the one before it, so allow 12 KB; a real leak is checked right below
    check("test sounds removed, free space back to %d KB (was %d KB; the /snd folder keeps 8 KB)" % (endq.free_kb, base_free), endq.free_kb >= base_free - 12, endq)
    ok, a = b.upload_kind(make_wav(1000, hz=900), 3, SND_BTN4, end_timeout=8.0)
    mid = b.query().free_kb
    b.remove_sound(SND_BTN4)
    check("no space leaks: a sound sent and removed again leaves the free space as it was (%d KB)" % endq.free_kb, ok and mid < endq.free_kb and b.query().free_kb == endq.free_kb, (ok, mid, endq.free_kb, b.query().free_kb))
    check("board alive after the sound tests", endq is not None, endq)


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
