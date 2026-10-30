#!/usr/bin/env python3
"""Reproduce the 'cheerful waiting' step of layout_smoke_test.py on its own and keep the WHOLE serial log when the board resets.
    ~/.venvs/pio39/bin/python -u scripts/waiting_cat_repro.py [--port P] [--seconds 100]"""
import argparse, os, sys, time
sys.path.insert(0, os.path.dirname(__file__))
from layout_tool import Board

CAT = b'{"v":1,"pg":[{"bg":{"t":"b","i":0},"w":[{"t":"dc","x":20,"y":20,"w":200,"h":40,"f":1,"z":1,"a":1},{"t":"ct","x":0,"y":180,"w":240,"h":132}]}]}'

ap = argparse.ArgumentParser()
ap.add_argument("--port")
ap.add_argument("--seconds", type=int, default=100)
a = ap.parse_args()
b = Board(a.port, boot_wait=3.5)
ok, msg = b.upload(CAT)
print("upload:", ok, msg)
log = b.read_text(a.seconds)
bad = [k for k in ("Guru Meditation", "abort()", "rst:", "Backtrace", "assert failed", "Task watchdog") if k in log]
print("markers:", bad or "none")
if bad:
    i = max(0, min(log.find(k) for k in bad if k in log) - 600)
    print("---- log around the first marker ----")
    print(log[i:i + 3500])
else:
    print("no reset in %d s; last lines:\n%s" % (a.seconds, log[-300:]))
b.reset_layout()
b.close()
