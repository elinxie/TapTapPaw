#!/usr/bin/env python3
"""Developer tool for the TapTapPaw layout upload protocol (docs/WIDGETS.md), over pyserial.

  layout_tool.py info   [--port P]            query: free space, stored layout crc, flags
  layout_tool.py upload FILE.json [--port P]  validate nothing locally, send, verify the crc read back from the board
  layout_tool.py reset  [--port P]            delete the stored layout (back to the built-in screens)
  layout_tool.py picture FILE.sjpg --slot N   upload a picture into slot 0..7
  layout_tool.py sound FILE.wav --slot N      upload a custom sound (canonical mono 16-bit WAV, <= 5 s) into sound slot 0..14
  layout_tool.py soundreset --slot N          delete only the custom copy of sound slot N (originals stay)
  layout_tool.py play --slot N                play sound slot N on the board

The normal way to do this is the web page; this exists for tests and for debugging without a browser.
Quit the TapTapPaw desktop app first: only one program can hold the serial port.
"""
import argparse, glob, struct, sys, time, zlib

try:
    import serial
except ImportError:
    sys.exit("pyserial missing. Use: ~/.venvs/pio39/bin/python scripts/layout_tool.py ...")

CHUNK = 192
VERSION = 1
STATUS = ["ok", "bad packet", "bad sequence", "too big", "checksum mismatch", "unsupported", "no space", "flash write error",
          "invalid layout", "no upload in progress", "incomplete"]
ERRORS = ["ok", "empty", "too big", "syntax error", "nested too deep", "unsupported version", "no pages", "too many pages", "bad page",
          "second main page", "too many widgets", "unknown widget type", "widget outside the 240x320 screen", "bad color",
          "value out of range", "bad text", "text pool full", "trailing data"]
ST = {n: i for i, n in enumerate(STATUS)}


def make_wav(samples, rate=16000, hz=880, amp=1200, silent=False):
    """Canonical 44-byte-header mono 16-bit WAV, the format the board accepts for kind 3 (docs/WIDGETS.md)."""
    import math
    pcm = b"".join(struct.pack("<h", 0 if silent else int(amp * math.sin(2 * math.pi * hz * i / rate))) for i in range(samples))
    hdr = b"RIFF" + struct.pack("<I", 36 + len(pcm)) + b"WAVEfmt " + struct.pack("<IHHIIHH", 16, 1, 1, rate, rate * 2, 2, 16)
    return hdr + b"data" + struct.pack("<I", len(pcm)) + pcm


def pkt(pid, payload=b""):
    return bytes([0xFF, pid, len(payload)]) + bytes(payload)


def begin_pkt(data, kind=0, slot=0, ver=VERSION, total=None, crc=None):
    total = len(data) if total is None else total
    crc = zlib.crc32(data) if crc is None else crc
    return pkt(0x0B, struct.pack("<BBBII", kind, slot, ver, total, crc))


def chunk_pkt(data, seq):
    return pkt(0x0C, struct.pack("<H", seq) + data[seq * CHUNK:(seq + 1) * CHUNK])


class Ack:
    def __init__(self, raw):
        _, _, _, self.cmd, self.status, self.detail, self.flags, self.seq, self.free_kb, self.crc = struct.unpack("<BBBBBBBHHI", raw)

    @property
    def status_name(self):
        return STATUS[self.status] if self.status < len(STATUS) else "status %d" % self.status

    @property
    def detail_name(self):
        return ERRORS[self.detail] if self.status == ST["invalid layout"] and self.detail < len(ERRORS) else ""

    def __repr__(self):
        return "Ack(cmd=0x%02X %s%s seq=%d free=%dKB crc=%08X flags=%d)" % (
            self.cmd, self.status_name, " (%s)" % self.detail_name if self.detail_name else "", self.seq, self.free_kb, self.crc, self.flags)


class Board:
    def __init__(self, port=None, boot_wait=3.0):
        port = port or (sorted(glob.glob("/dev/cu.usbserial-*")) or [None])[0]
        if not port:
            sys.exit("no /dev/cu.usbserial-* port found; pass --port")
        self.port = port
        self.ser = serial.Serial(port, 115200, timeout=0.005)  # opening usually resets the ESP32
        time.sleep(boot_wait)
        self.ser.reset_input_buffer()
        self.buf = b""

    def close(self):
        self.ser.close()

    def hard_reset(self, boot_wait=3.5):
        """Reboot the ESP32 through the RTS/DTR auto-reset circuit (like pressing EN)."""
        self.ser.dtr = False
        self.ser.rts = True
        time.sleep(0.15)
        self.ser.rts = False
        time.sleep(boot_wait)
        self.ser.reset_input_buffer()
        self.buf = b""

    def read_text(self, seconds):
        """Collect serial output for a while (firmware log lines); ack frames stay in self.buf."""
        end = time.time() + seconds
        text = b""
        while time.time() < end:
            d = self.ser.read(512)
            text += d
            self.buf += d
        return text.decode("latin-1")

    def send(self, data):
        self.ser.write(data)
        self.ser.flush()

    def ack(self, cmd, timeout=2.0):
        """Wait for a 0x10 ack for `cmd`; other serial text (boot log) is skipped."""
        end = time.time() + timeout
        while time.time() < end:
            self.buf += self.ser.read(256)
            i = self.buf.find(b"\xFF\x10\x0C")
            while i >= 0 and len(self.buf) >= i + 15:
                a = Ack(self.buf[i:i + 15])
                self.buf = self.buf[i + 15:]
                if a.cmd == cmd:
                    return a
                i = self.buf.find(b"\xFF\x10\x0C")
            if i > 0:
                self.buf = self.buf[i:]
        return None

    def call(self, packet, cmd, timeout=2.0, tries=3):
        for _ in range(tries):
            self.send(packet)
            a = self.ack(cmd, timeout)
            if a:
                return a
        return None

    def query(self):
        return self.call(pkt(0x0F), 0x0F)

    def reset_layout(self):
        return self.call(pkt(0x11), 0x11)

    def abort(self):
        return self.call(pkt(0x0E), 0x0E)

    def send_chunks(self, data, seq, window=1):
        """Sliding window: up to `window` chunks in flight. Keep window=1 for the real board: measured, more than one chunk
        in flight loses bytes while the board writes flash (the end checksum catches it). The board acks every
        chunk with the next sequence it expects; a gap answers "bad sequence" and the sender rewinds to it. Returns the last ack."""
        n = (len(data) + CHUNK - 1) // CHUNK
        acked, sent, a, stalls = seq, seq, None, 0
        while acked < n:
            while sent < n and sent - acked < window:
                self.send(chunk_pkt(data, sent))
                sent += 1
            a = self.ack(0x0C, 2.0)
            if a is None:  # lost: resend from the last acknowledged chunk
                stalls += 1
                if stalls > 3:
                    return None
                self.buf = b""
                sent = acked
                continue
            if a.status == ST["bad sequence"]:
                if a.seq < acked or a.seq > sent:
                    continue  # stale answer to a chunk already handled
                acked = sent = a.seq  # rewind to what the board wants
                self.drain(0.05)
                continue
            if a.status != 0:
                return a
            acked = max(acked, a.seq)
            stalls = 0
        return a

    def drain(self, seconds):
        """Read and drop acks still on the way (after a rewind)."""
        end = time.time() + seconds
        while time.time() < end:
            self.ser.read(256)
        self.buf = b""

    def upload_kind(self, data, kind, slot, window=1, end_timeout=5.0):
        """Picture (kind 1, slot 0..7), album art (kind 2) or custom sound (kind 3, slot 0..14). The END ack carries the crc the board read back."""
        a = self.call(begin_pkt(data, kind=kind, slot=slot), 0x0B)
        if not a or a.status != 0:
            return False, a
        a = self.send_chunks(data, a.seq, window)
        if not a or a.status not in (0, ST["bad sequence"]):
            return False, a
        a = self.call(pkt(0x0D), 0x0D, timeout=end_timeout)  # the board test-decodes the picture / reads the sound back before it answers
        return bool(a and a.status == 0 and a.crc == zlib.crc32(data)), a

    def picture_crc(self, slot):
        return self.call(pkt(0x0F, bytes([slot])), 0x0F)

    def remove_picture(self, slot):
        return self.call(pkt(0x11, bytes([slot])), 0x11)

    def sound_crc(self, slot):
        """QUERY [3, slot]: crc of that custom sound (0 = none), detail = its size in KB."""
        return self.call(pkt(0x0F, bytes([3, slot])), 0x0F)

    def remove_sound(self, slot):
        """RESET [3, slot]: delete the custom copy only; the original sound file is not touched."""
        return self.call(pkt(0x11, bytes([3, slot])), 0x11)

    def play(self, slot):
        return self.call(pkt(0x14, bytes([slot])), 0x14)

    def upload(self, data, log=print):
        n = (len(data) + CHUNK - 1) // CHUNK
        a = self.call(begin_pkt(data), 0x0B)
        if not a or a.status != 0:
            return False, "begin: %r" % a
        seq = a.seq
        while seq < n:
            a = self.call(chunk_pkt(data, seq), 0x0C)
            if not a:
                return False, "chunk %d: no answer" % seq
            if a.status == ST["bad sequence"]:
                seq = a.seq
                continue
            if a.status != 0:
                return False, "chunk %d: %r" % (seq, a)
            seq = a.seq
        a = self.call(pkt(0x0D), 0x0D)
        if not a or a.status != 0:
            return False, "end: %r" % a
        q = self.query()
        if not q or q.crc != zlib.crc32(data):
            return False, "verify: %r" % q
        return True, "stored and verified, crc %08X, free %d KB" % (q.crc, q.free_kb)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=["info", "upload", "reset", "picture", "sound", "soundreset", "play"])
    ap.add_argument("--slot", type=int, default=0, help="picture slot 0..7 (picture), sound slot 0..14 (sound, soundreset, play)")
    ap.add_argument("file", nargs="?")
    ap.add_argument("--port")
    args = ap.parse_args()
    b = Board(args.port)
    try:
        if args.cmd == "info":
            print(b.query())
        elif args.cmd == "reset":
            print(b.reset_layout())
        elif args.cmd == "sound":
            ok, a = b.upload_kind(open(args.file, "rb").read(), 3, args.slot, end_timeout=8.0)
            print(("OK: " if ok else "FAILED: ") + repr(a))
            sys.exit(0 if ok else 1)
        elif args.cmd == "soundreset":
            print(b.remove_sound(args.slot))
        elif args.cmd == "play":
            print(b.play(args.slot))
        elif args.cmd == "picture":
            ok, a = b.upload_kind(open(args.file, "rb").read(), 1, args.slot)
            print(("OK: " if ok else "FAILED: ") + repr(a))
            sys.exit(0 if ok else 1)
        else:
            data = open(args.file, "rb").read().strip()
            ok, msg = b.upload(data)
            print(("OK: " if ok else "FAILED: ") + msg)
            sys.exit(0 if ok else 1)
    finally:
        b.close()


if __name__ == "__main__":
    main()
