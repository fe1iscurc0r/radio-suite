#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""bus_client 用例：只依赖标准库，用一个假总线把四类失败都演一遍.

跑法（本机或 CI 都一样）：
    python -m unittest discover -s tools/mcp_radio/tests -t tools/mcp_radio/tests
"""
from __future__ import annotations

import json
import os
import socket
import sys
import threading
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

sys.path.insert(0, str(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))))

from mcp_radio.bus_client import (  # noqa: E402
    BusClient,
    BusConfigError,
    BusError,
    BusUnauthorized,
    BusUnavailable,
    base_url_from_env,
)

STATE = {"freq_hz": 14074000.0, "mode": "FT8", "filter": 3000, "ptt_on": False,
         "smeter_raw": 12, "smeter_dbm": -78.0, "att_20db": False, "nb": False,
         "nr": True, "last_update_ms": 1_730_000_000_000}
SPECTRUM = {"frame_seq": 7, "bin_hz": 23.4375, "power_db": [-100.0, -90.5, -42.0]}

TOKEN = "s3cr3t-do-not-echo"


class Handler(BaseHTTPRequestHandler):
    token_required = ""

    def do_GET(self):  # noqa: N802
        if self.token_required:
            if self.headers.get("X-Radio-Token") != self.token_required:
                self.send_response(401)
                self.end_headers()
                return
        if self.path == "/api/state":
            body, mime = json.dumps(STATE), "application/json"
        elif self.path == "/api/spectrum":
            body, mime = json.dumps(SPECTRUM), "application/json"
        elif self.path == "/api/binary":
            body, mime = b"\x00\x01not json", "application/octet-stream"
        else:
            self.send_response(404)
            self.end_headers()
            return
        raw = body if isinstance(body, bytes) else body.encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", mime)
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)

    def log_message(self, *args):  # 关掉 stderr 噪音
        pass


class FakeBus:
    def __enter__(self):
        self.server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.port = self.server.server_address[1]
        self.url = f"http://127.0.0.1:{self.port}"
        return self

    def __exit__(self, *exc):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=2)


class ClientTests(unittest.TestCase):
    def setUp(self):
        Handler.token_required = ""

    def test_reads_state_and_spectrum(self):
        with FakeBus() as bus:
            c = BusClient(base_url=bus.url)
            self.assertEqual(c.state(), STATE)
            self.assertEqual(c.spectrum(), SPECTRUM)

    def test_token_goes_in_header_not_url(self):
        seen = {}

        class Spy(Handler):
            def do_GET(self):  # noqa: N802
                seen["header"] = self.headers.get("X-Radio-Token")
                seen["query"] = self.path
                Handler.do_GET(self)

        with FakeBus() as bus:
            bus.server.RequestHandlerClass = Spy
            Handler.token_required = TOKEN
            c = BusClient(base_url=bus.url, token=TOKEN)
            self.assertEqual(c.state(), STATE)
            self.assertEqual(seen["header"], TOKEN)
            self.assertNotIn(TOKEN, seen["query"])  # ?token= 会进日志，我们不用

    def test_401_is_unauthorized_and_never_echoes_token(self):
        with FakeBus() as bus:
            Handler.token_required = TOKEN
            c = BusClient(base_url=bus.url, token="wrong")
            with self.assertRaises(BusUnauthorized) as ctx:
                c.state()
            self.assertNotIn(TOKEN, str(ctx.exception))
            self.assertNotIn("wrong", str(ctx.exception))

    def test_dead_bus_is_unavailable(self):
        with FakeBus() as bus:
            port = bus.port
        # 和 C++ 那条死端口用例同一个教训：放开的临时端口不保证还空着，先探一次
        with socket.socket() as peek:
            peek.settimeout(0.25)
            if peek.connect_ex(("127.0.0.1", port)) == 0:
                self.skipTest("这个临时端口放手后立刻被人占了，换不出可确认的死端口")
        c = BusClient(base_url=f"http://127.0.0.1:{port}", timeout_s=1.0)
        with self.assertRaises(BusUnavailable):
            c.state()

    def test_non_json_body_is_bus_error(self):
        with FakeBus() as bus:
            c = BusClient(base_url=bus.url)
            with self.assertRaises(BusError):
                c.get_json("/api/binary")

    def test_missing_endpoint_is_bus_error(self):
        with FakeBus() as bus:
            c = BusClient(base_url=bus.url)
            with self.assertRaises(BusError) as ctx:
                c.get_json("/api/nope")
            self.assertIn("404", str(ctx.exception))

    def test_remote_bus_without_token_is_refused_at_construction(self):
        # 总线那边「非环回绑定必须带 token」，客户端先挡一道
        with self.assertRaises(BusConfigError):
            BusClient(base_url="http://192.168.1.20:8765", token="", env={})
        c = BusClient(base_url="http://192.168.1.20:8765", token="t", env={})
        self.assertEqual(c.token, "t")

    def test_env_defaults(self):
        self.assertEqual(base_url_from_env({}), "http://127.0.0.1:8765")
        self.assertEqual(base_url_from_env({"RADIO_BUS_PORT": "9000"}), "http://127.0.0.1:9000")
        self.assertEqual(base_url_from_env({"RADIO_BUS_URL": "http://127.0.0.1:1234"}),
                         "http://127.0.0.1:1234")
        with self.assertRaises(BusConfigError):
            base_url_from_env({"RADIO_BUS_PORT": "8765; rm -rf /"})
        with self.assertRaises(BusConfigError):
            BusClient(base_url="https://127.0.0.1:8765")  # 总线是明文 HTTP，别假装

    def test_timeout_env_is_milliseconds(self):
        c = BusClient(base_url="http://127.0.0.1:1", env={"RADIO_BUS_TIMEOUT_MS": "250"})
        self.assertAlmostEqual(c.timeout_s, 0.25)


if __name__ == "__main__":
    unittest.main()
