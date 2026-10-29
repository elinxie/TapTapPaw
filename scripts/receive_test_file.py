#!/usr/bin/env python3
"""Test helper only: accepts one POST on 127.0.0.1:8792 and writes its body to the given file, so bytes made by the web page
in Chrome (e.g. an SJPG) can be pushed to the board with layout_tool.py. Not used by the page itself."""
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer

out = sys.argv[1]
class H(BaseHTTPRequestHandler):
    def do_OPTIONS(self):
        self.send_response(204); self.send_header("Access-Control-Allow-Origin", "*"); self.send_header("Access-Control-Allow-Methods", "POST"); self.end_headers()
    def do_POST(self):
        n = int(self.headers.get("Content-Length", 0))
        open(out, "wb").write(self.rfile.read(n))
        self.send_response(200); self.send_header("Access-Control-Allow-Origin", "*"); self.end_headers(); self.wfile.write(b"saved")
        print("saved %d bytes to %s" % (n, out), flush=True)
        raise SystemExit
HTTPServer(("127.0.0.1", 8792), H).serve_forever()
