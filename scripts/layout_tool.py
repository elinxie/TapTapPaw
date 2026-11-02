#!/usr/bin/env python3
"""Developer tool for the TapTapPaw layout upload protocol (docs/WIDGETS.md), over pyserial.

  layout_tool.py info   [--port P]            query: free space, stored layout crc, flags
  layout_tool.py upload FILE.json [--port P]  validate nothing locally, send, verify the crc read back from the board
  layout_tool.py reset  [--port P]            delete the stored layout (back to the built-in screens)

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
        self.ser = serial.Serial(port, 115200, timeout=0.05)  # opening usually resets the ESP32
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
    ap.add_argument("cmd", choices=["info", "upload", "reset"])
    ap.add_argument("file", nargs="?")
    ap.add_argument("--port")
    args = ap.parse_args()
    b = Board(args.port)
    try:
        if args.cmd == "info":
            print(b.query())
        elif args.cmd == "reset":
            print(b.reset_layout())
        else:
            data = open(args.file, "rb").read().strip()
            ok, msg = b.upload(data)
            print(("OK: " if ok else "FAILED: ") + msg)
            sys.exit(0 if ok else 1)
    finally:
        b.close()


if __name__ == "__main__":
    main()
