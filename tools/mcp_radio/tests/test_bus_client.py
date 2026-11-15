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
    posts: list = []          # 记下 (path, body, content_type)：数得出「一次调用发了几个请求」
    control_reply = None      # 覆盖 /api/control 的回包，用来演各种坏形状

    def _authorized(self) -> bool:
        if self.token_required and self.headers.get("X-Radio-Token") != self.token_required:
            self.send_response(401)
            self.end_headers()
            return False
        return True

    def _send(self, body, mime="application/json", status=200):
        raw = body if isinstance(body, bytes) else body.encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", mime)
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)

    def do_GET(self):  # noqa: N802
        if not self._authorized():
            return
        if self.path == "/api/state":
            self._send(json.dumps(STATE))
        elif self.path == "/api/spectrum":
            self._send(json.dumps(SPECTRUM))
        elif self.path == "/api/qso.adif":
            self._send("ADIF 导出\n<CALL:6>BG1ABC <EOR>\n", "text/plain")
        elif self.path == "/api/binary":
            self._send(b"\x00\x01not json", "application/octet-stream")
        else:
            self.send_response(404)
            self.end_headers()

    def do_POST(self):  # noqa: N802
        if not self._authorized():
            return
        raw = self.rfile.read(int(self.headers.get("Content-Length") or 0))
        try:
            body = json.loads(raw.decode("utf-8")) if raw else None
        except ValueError:
            body = None
        Handler.posts.append((self.path, body, self.headers.get("Content-Type")))
        if self.path == "/api/binary":
            self._send(b"\x00\x01not json", "application/octet-stream")
            return
        if self.path == "/api/control":
            if Handler.control_reply is not None:
                self._send(json.dumps(Handler.control_reply))
                return
            action = (body or {}).get("action")
            results = {
                "ptt_on": {"ptt": "confirm_pending"},
                "ptt_off": {"ptt": "rx"},
                "tune": {"tuned": True, "target_hz": (body or {}).get("freq")},
                "set_mode": {"mode": (body or {}).get("mode")},
            }
            if action in results:
                self._send(json.dumps({"ok": True, "result": results[action]}))
            else:
                self._send(json.dumps({"error": "unknown action"}))
            return
        if self.path == "/api/qso":
            qso = (body or {}).get("qso") or {}
            if not str(qso.get("callsign", "")).strip():
                self._send(json.dumps({"error": "callsign required"}))
            else:
                self._send(json.dumps({"ok": True, "count": 1}))
            return
        self.send_response(404)
        self.end_headers()

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
        Handler.posts = []
        Handler.control_reply = None

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


class WriteClientTests(unittest.TestCase):
    """写出口（M2）：一次调用一个请求，坏形状要变成 BusError 而不是静默成功。"""

    def setUp(self):
        Handler.token_required = ""
        Handler.posts = []
        Handler.control_reply = None

    def test_control_posts_json_and_unwraps_result(self):
        with FakeBus() as bus:
            c = BusClient(base_url=bus.url)
            result = c.control("tune", freq=7_100_000)
            self.assertEqual(result, {"tuned": True, "target_hz": 7_100_000})
            self.assertEqual(Handler.posts,
                             [("/api/control", {"action": "tune", "freq": 7_100_000},
                               "application/json")])

    def test_one_call_is_one_request_no_hidden_retry(self):
        with FakeBus() as bus:
            c = BusClient(base_url=bus.url)
            c.control("ptt_on")
            c.control("ptt_on")
            # 两击门禁的前提是「两次独立的决定」；封装里偷偷补发等于把门禁拆了
            self.assertEqual([p[1]["action"] for p in Handler.posts], ["ptt_on", "ptt_on"])
            self.assertEqual(len(Handler.posts), 2)

    def test_control_without_ok_is_bus_error(self):
        with FakeBus() as bus:
            Handler.control_reply = {"error": "bad mode"}
            c = BusClient(base_url=bus.url)
            with self.assertRaises(BusError) as ctx:
                c.control("set_mode", mode="NOPE")
            self.assertIn("bad mode", str(ctx.exception))

    def test_control_missing_result_is_bus_error(self):
        with FakeBus() as bus:
            Handler.control_reply = {"ok": True}
            c = BusClient(base_url=bus.url)
            with self.assertRaises(BusError):
                c.control("ptt_on")

    def test_post_401_never_echoes_token(self):
        with FakeBus() as bus:
            Handler.token_required = TOKEN
            c = BusClient(base_url=bus.url, token="wrong")
            with self.assertRaises(BusUnauthorized) as ctx:
                c.control("ptt_on")
            self.assertNotIn(TOKEN, str(ctx.exception))
            self.assertNotIn("wrong", str(ctx.exception))
            self.assertEqual(Handler.posts, [])

    def test_token_goes_in_post_header_not_url(self):
        with FakeBus() as bus:
            Handler.token_required = TOKEN
            c = BusClient(base_url=bus.url, token=TOKEN)
            c.control("ptt_off")
            self.assertEqual(len(Handler.posts), 1)
            self.assertNotIn(TOKEN, Handler.posts[0][0])

    def test_post_non_json_body_is_bus_error(self):
        with FakeBus() as bus:
            c = BusClient(base_url=bus.url)
            with self.assertRaises(BusError):
                c.post_json("/api/binary", {"action": "ptt_on"})

    def test_log_qso_uses_the_qso_envelope(self):
        with FakeBus() as bus:
            c = BusClient(base_url=bus.url)
            reply = c.log_qso({"callsign": "BG1ABC", "freq_mhz": "14.27"})
            self.assertEqual(reply["count"], 1)
            self.assertEqual(Handler.posts[0][1],
                             {"qso": {"callsign": "BG1ABC", "freq_mhz": "14.27"}})

    def test_log_qso_without_callsign_is_bus_error(self):
        with FakeBus() as bus:
            c = BusClient(base_url=bus.url)
            with self.assertRaises(BusError) as ctx:
                c.log_qso({"callsign": "  "})
            self.assertIn("callsign required", str(ctx.exception))

    def test_qso_adif_is_text_not_json(self):
        with FakeBus() as bus:
            c = BusClient(base_url=bus.url)
            text = c.qso_adif()
            self.assertIn("<CALL:6>BG1ABC", text)
            self.assertIn("导出", text)  # 中文按 UTF-8 解，别按本地代码页


if __name__ == "__main__":
    unittest.main()
