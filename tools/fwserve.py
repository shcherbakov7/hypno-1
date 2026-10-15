#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""HYPNO-1: serve build/ on http://localhost:8770, no caching: every fetch gets the latest build.
A copy of the fm1-emu emulator page (joakimeriksson.github.io/fm1-emu, MIT) in build/emu-site/ plays it:
open http://localhost:8770/emu-site/?fw=hypno and press Start. Its firmware list gets "HYPNO-1 (dev)" =
build/felucca.fwsc here, the rest as on the site.
    python3 tools/fwserve.py [PORT]"""
import functools
import http.server
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent / "build"
HYPNO = {"id": "hypno", "name": "HYPNO-1 (dev)", "version": "dev", "author": "HYPNO-1", "license": "GPL-3.0",
         "about": "This tree's build/felucca.fwsc.", "repo": "https://github.com/shcherbakov7/hypno-1",
         "package": ""}


class Handler(http.server.SimpleHTTPRequestHandler):
    def do_GET(self):
        if self.path.split("?")[0] == "/emu-site/firmwares.json":
            cat = json.loads((ROOT / "emu-site" / "firmwares.json").read_text())
            me = dict(HYPNO, package="http://%s/felucca.fwsc" % self.headers.get("Host", "localhost:8770"))   # (a full URL)
            cat["firmwares"] = [me] + [f for f in cat["firmwares"] if f.get("id") != "hypno"]
            body = json.dumps(cat).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        super().do_GET()

    def end_headers(self):
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Private-Network", "true")
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def do_OPTIONS(self):
        self.send_response(204)
        self.send_header("Access-Control-Allow-Methods", "GET")
        self.send_header("Access-Control-Allow-Headers", "*")
        self.end_headers()


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8770
    http.server.ThreadingHTTPServer(("127.0.0.1", port), functools.partial(Handler, directory=str(ROOT))).serve_forever()
