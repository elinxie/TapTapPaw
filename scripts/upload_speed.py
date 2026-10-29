#!/usr/bin/env python3
"""Measures picture upload speed for different window sizes (real board). Leaves slot 7 empty afterwards.
Measured 2026-10-29: window 1 = 3.9 KB/s; windows 2-8 fail the end checksum (bytes lost while flash is written)."""
import os, sys, time
sys.path.insert(0, os.path.dirname(__file__))
from layout_tool import Board
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
data = open(os.path.join(ROOT, "sketch/test/fixtures/pic_240x320.sjpg"), "rb").read()
b = Board(sys.argv[1] if len(sys.argv) > 1 else None)
for window in (1, 2, 4, 8):
    t0 = time.time()
    ok, a = b.upload_kind(data, 1, 7, window)
    dt = time.time() - t0
    print("window %d: %s  %d B in %.2f s = %.1f KB/s" % (window, "ok" if ok else "FAILED %r" % (a,), len(data), dt, len(data) / 1024 / dt))
print(b.remove_picture(7))
b.close()
