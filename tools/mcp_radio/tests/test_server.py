#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""server 用例：不联网、不起 Qt，只把 dispatch 当纯函数打.

覆盖三件事：① JSON-RPC/MCP 骨架本身（握手、tools/list、错误码、通知吞掉）；
② 三个只读工具的字段与折算（频谱折叠、峰位换算、link_health 的帧号推进）；
③ 边界：std 输出只允许协议帧，日志必须走 stderr（官方规范里一行 print() 就能毁掉一帧）。
"""
from __future__ import annotations

import io
import json
import os
import subprocess
import sys
import unittest

sys.path.insert(0, str(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))))

from mcp_radio import server  # noqa: E402
from mcp_radio.bus_client import BusError, BusUnauthorized  # noqa: E402

NOW_MS = 1_730_000_000_000


class StubBus:
    """假总线：把两击门禁的状态机照搬一份，并记下每一次 control 调用。

    记调用次数是这套用例的关键——「适配器不能一次调用就发射」只有数得出
    「一次 tools/call 到底往总线发了几个动作」才算证明，光看返回字段不算。
    """

    base_url = "http://127.0.0.1:8765"
    token = ""
    MODES = ("LSB", "USB", "AM", "CW", "RTTY", "FM", "WFM")

    def __init__(self, state=None, spectrum_seq=(100, 103), bins=None,
                 tx_allowed=True, confirm_expires=False):
        self.state_value = {
            "freq_hz": 14074000.0, "mode": "FT8", "filter": 3000, "ptt_on": False,
            "ptt_phase": "rx",
            "smeter_raw": 12, "smeter_dbm": -78.0, "att_20db": False, "nb": False,
            "nr": True, "last_update_ms": NOW_MS - 120,
        }
        if state is not None:
            self.state_value = state
        self.seq_list = list(spectrum_seq)
        self.bins = bins if bins is not None else [-100.0 + i * 0.1 for i in range(1025)]
        self.spectrum_calls = 0
        self.raise_on_state = None
        self.control_calls: list[tuple[str, dict]] = []
        self.qso_calls: list[dict] = []
        self.adif_text = "ADIF 导出\n<CALL:6>BG1ABC <EOR>\n"
        self.phase = self.state_value.get("ptt_phase", "rx")
        self.tx_allowed = tx_allowed
        self.confirm_expires = confirm_expires

    def state(self):
        if self.raise_on_state:
            raise self.raise_on_state
        return dict(self.state_value)

    def spectrum(self):
        seq = self.seq_list[min(self.spectrum_calls, len(self.seq_list) - 1)]
        self.spectrum_calls += 1
        return {"frame_seq": seq, "bin_hz": 23.4375, "power_db": list(self.bins)}

    def _set_phase(self, phase):
        self.phase = phase
        self.state_value["ptt_phase"] = phase
        self.state_value["ptt_on"] = phase == "tx"

    def control(self, action, **fields):
        self.control_calls.append((action, dict(fields)))
        if action == "ptt_on":
            if self.phase == "rx":
                self._set_phase("confirm_pending")
            elif self.phase == "confirm_pending":
                if self.confirm_expires:
                    self._set_phase("confirm_pending")  # 窗口过期：这一下只是重新武装
                else:
                    self._set_phase("tx" if self.tx_allowed else "rx")
            else:
                self._set_phase("rx")
            return {"ptt": self.phase}
        if action == "ptt_off":
            self._set_phase("rx")
            return {"ptt": "rx"}
        if action == "tune":
            hz = int(fields["freq"])
            if not 30_000 <= hz <= 199_999_999:
                return {"tuned": False, "target_hz": float(hz), "error": "rejected"}
            self.state_value["freq_hz"] = float(hz)
            return {"tuned": True, "target_hz": float(hz)}
        if action == "set_mode":
            mode = str(fields["mode"]).strip().upper()
            if mode not in self.MODES:
                raise BusError("总线拒绝 set_mode：bad mode")
            self.state_value["mode"] = mode
            return {"mode": mode}
        raise BusError(f"总线拒绝 {action}：unknown action")

    def log_qso(self, qso):
        if not str(qso.get("callsign", "")).strip():
            raise BusError("总线拒绝记账：callsign required")
        self.qso_calls.append(dict(qso))
        return {"ok": True, "count": len(self.qso_calls)}

    def qso_adif(self):
        return self.adif_text


def rpc(method, params=None, req_id=1):
    msg = {"jsonrpc": "2.0", "method": method, "id": req_id}
    if params is not None:
        msg["params"] = params
    return msg


def call_tool(name, args=None, bus=None):
    bus = bus or StubBus()
    reply = server.dispatch(rpc("tools/call", {"name": name, "arguments": args or {}}), bus,
                            now_ms=lambda: NOW_MS)
    assert reply is not None
    return reply, bus


class ProtocolSkeletonTests(unittest.TestCase):
    def test_initialize_echoes_supported_version(self):
        reply = server.dispatch(rpc("initialize", {"protocolVersion": "2026-07-28",
                                                   "capabilities": {},
                                                   "clientInfo": {"name": "t", "version": "0"}}),
                                StubBus())
        self.assertEqual(reply["result"]["protocolVersion"], "2026-07-28")
        self.assertIn("tools", reply["result"]["capabilities"])
        self.assertEqual(reply["result"]["serverInfo"]["name"], "radio-suite")

    def test_initialize_falls_back_to_latest_when_version_unknown(self):
        reply = server.dispatch(rpc("initialize", {"protocolVersion": "1999-01-01"}), StubBus())
        self.assertEqual(reply["result"]["protocolVersion"], server.LATEST_PROTOCOL)

    def test_no_initialize_needed_for_tools_list(self):
        # 2026-07-28 是无状态的：没握手也必须能直接列工具
        reply = server.dispatch(rpc("tools/list"), StubBus())
        names = [t["name"] for t in reply["result"]["tools"]]
        self.assertEqual(names, ["get_state", "get_spectrum", "link_health",
                                 "tune", "set_mode", "ptt", "log_qso", "export_adif"])
        for tool in reply["result"]["tools"]:
            self.assertTrue(tool["description"])
            self.assertEqual(tool["inputSchema"]["additionalProperties"], False)
            self.assertEqual(tool["annotations"]["readOnlyHint"],
                             tool["name"] in ("get_state", "get_spectrum", "link_health",
                                              "export_adif"), tool["name"])

    def test_write_tools_are_not_marked_read_only_and_ptt_is_destructive(self):
        reply = server.dispatch(rpc("tools/list"), StubBus())
        by_name = {t["name"]: t for t in reply["result"]["tools"]}
        # 标注是给宿主看的「这工具会不会改世界」；写工具标成只读等于骗宿主放行
        for name in ("tune", "set_mode", "ptt", "log_qso"):
            self.assertFalse(by_name[name]["annotations"]["readOnlyHint"], name)
        self.assertTrue(by_name["ptt"]["annotations"]["destructiveHint"])
        self.assertFalse(by_name["tune"]["annotations"]["destructiveHint"])
        self.assertIn("confirm", by_name["ptt"]["inputSchema"]["properties"])
        self.assertEqual(by_name["ptt"]["inputSchema"]["required"], ["action"])

    def test_notifications_get_no_reply(self):
        for method in ("notifications/initialized", "notifications/cancelled", "whatever"):
            msg = {"jsonrpc": "2.0", "method": method}
            self.assertIsNone(server.dispatch(msg, StubBus()), method)

    def test_unknown_method_and_bad_frame(self):
        self.assertEqual(server.dispatch(rpc("prompts/list"), StubBus())["error"]["code"],
                         server.METHOD_NOT_FOUND)
        self.assertEqual(server.dispatch({"method": "no-jsonrpc"}, StubBus())["error"]["code"],
                         server.INVALID_REQUEST)
        self.assertEqual(server.dispatch(rpc("tools/call", {"name": "set_freq"}), StubBus())["error"]["code"],
                         server.INVALID_PARAMS)

    def test_ping(self):
        self.assertEqual(server.dispatch(rpc("ping"), StubBus())["result"], {})


class ReadToolTests(unittest.TestCase):
    def test_get_state_adds_mhz_and_age(self):
        reply, _ = call_tool("get_state")
        payload = reply["result"]["structuredContent"]
        self.assertFalse(reply["result"]["isError"])
        self.assertEqual(payload["freq_mhz"], 14.074)
        self.assertEqual(payload["state_age_ms"], 120)
        self.assertEqual(payload["mode"], "FT8")
        # 文本与结构化必须同源，别让宿主读到两套数
        self.assertEqual(json.loads(reply["result"]["content"][0]["text"]), payload)

    def test_spectrum_axis_is_audio_not_rf(self):
        bins = [-120.0] * 1025
        bins[320] = -30.0  # 7500 Hz 附近
        reply, _ = call_tool("get_spectrum", {"max_points": 8}, StubBus(bins=bins))
        payload = reply["result"]["structuredContent"]
        self.assertEqual(payload["axis"]["kind"], "audio_baseband_hz")
        self.assertAlmostEqual(payload["peak_hz"], 320 * 23.4375, places=3)
        self.assertEqual(payload["peak_db"], -30.0)
        self.assertAlmostEqual(payload["axis"]["to_hz"], 23.4375 * 1024, places=3)
        self.assertEqual(len(payload["power_db_peaks"]), 8)

    def test_folding_keeps_the_peak_of_each_bucket(self):
        values = list(range(100))
        folded = server._fold_peaks(values, 4)
        self.assertEqual(folded, [24.0, 49.0, 74.0, 99.0])
        self.assertEqual(len(server._fold_peaks(values, 512)), 100)  # 不超过原长就别压

    def test_spectrum_rejects_bad_max_points(self):
        for bad in (0, 513, "64", True):
            reply, _ = call_tool("get_spectrum", {"max_points": bad})
            self.assertTrue(reply["result"]["isError"], bad)
            self.assertIn("max_points", reply["result"]["content"][0]["text"])

    def test_link_health_reads_frame_progress_and_warns(self):
        reply, bus = call_tool("link_health", {"window_ms": 0}, StubBus(spectrum_seq=(100, 103)))
        payload = reply["result"]["structuredContent"]
        self.assertEqual(bus.spectrum_calls, 2)
        self.assertTrue(payload["spectrum_advancing"])
        self.assertEqual(payload["frames_in_window"], 3)
        # 速率必须用实测间隔算：window_ms=0 时把分母写成 1 ms 就会把 3 帧报成 3000 fps
        self.assertGreaterEqual(payload["measured_window_ms"], 1)
        self.assertAlmostEqual(
            payload["approx_frames_per_s"],
            round(3 * 1000.0 / payload["measured_window_ms"], 1),
        )
        self.assertEqual(payload["verdict"], "ok")
        self.assertIn("别把 ok 读成", payload["scope_note"])

    def test_link_health_flags_stalled_and_tx(self):
        reply, _ = call_tool("link_health", {"window_ms": 0},
                             StubBus(spectrum_seq=(100, 100),
                                     state={"freq_hz": 3.5e6, "mode": "USB", "ptt_on": True,
                                            "last_update_ms": NOW_MS - 60_000}))
        payload = reply["result"]["structuredContent"]
        self.assertFalse(payload["spectrum_advancing"])
        self.assertEqual(payload["verdict"], "degraded")
        joined = " ".join(payload["warnings"])
        self.assertIn("没推进", joined)
        self.assertIn("60000 ms", joined)
        self.assertIn("发射", joined)

    def test_link_health_rejects_bad_window(self):
        for bad in (-1, 2001, "200"):
            reply, _ = call_tool("link_health", {"window_ms": bad})
            self.assertTrue(reply["result"]["isError"], bad)

    def test_bus_failure_becomes_tool_error_not_protocol_crash(self):
        reply, _ = call_tool("get_state")
        self.assertFalse(reply["result"]["isError"])
        boom = StubBus()
        boom.raise_on_state = BusUnauthorized("总线返回 401：token 缺失或不匹配")
        reply, _ = call_tool("get_state", bus=boom)
        self.assertTrue(reply["result"]["isError"])
        self.assertIn("BusUnauthorized", reply["result"]["content"][0]["text"])


class PttGateTests(unittest.TestCase):
    """M2 的核心验收：一次 tools/call 绝不可能把电台从 RX 按成发射。"""

    def test_first_call_only_arms_and_sends_exactly_one_press(self):
        reply, bus = call_tool("ptt", {"action": "on"})
        payload = reply["result"]["structuredContent"]
        self.assertFalse(reply["result"]["isError"])
        self.assertEqual(payload["ptt_phase"], "confirm_pending")
        self.assertFalse(payload["transmitting"])
        self.assertEqual(payload["phase_before"], "rx")
        self.assertEqual(bus.control_calls, [("ptt_on", {})])
        self.assertFalse(bus.state_value["ptt_on"])

    def test_confirm_from_rx_is_refused_without_sending_anything(self):
        # 最要紧的一条：带 confirm=true 但门禁没武装时，连「武装」都不许替它做，
        # 否则一次调用就成了 Rx→待确认→Tx，两击门禁形同虚设。
        reply, bus = call_tool("ptt", {"action": "on", "confirm": True})
        self.assertTrue(reply["result"]["isError"])
        self.assertEqual(bus.control_calls, [])
        self.assertEqual(bus.phase, "rx")
        self.assertIn("不是待确认", reply["result"]["content"][0]["text"])

    def test_two_separate_calls_are_required_to_transmit(self):
        bus = StubBus()
        first, _ = call_tool("ptt", {"action": "on"}, bus)
        self.assertEqual(first["result"]["structuredContent"]["ptt_phase"], "confirm_pending")
        second, _ = call_tool("ptt", {"action": "on", "confirm": True}, bus)
        payload = second["result"]["structuredContent"]
        self.assertFalse(second["result"]["isError"])
        self.assertEqual(payload["ptt_phase"], "tx")
        self.assertTrue(payload["transmitting"])
        self.assertEqual(payload["phase_before"], "confirm_pending")
        self.assertEqual(bus.control_calls, [("ptt_on", {}), ("ptt_on", {})])

    def test_armed_gate_without_confirm_is_refused(self):
        # 已武装时不带 confirm 再调一次，总线那一下就是确认发射——必须提前拦住
        bus = StubBus()
        call_tool("ptt", {"action": "on"}, bus)
        reply, _ = call_tool("ptt", {"action": "on"}, bus)
        self.assertTrue(reply["result"]["isError"])
        self.assertEqual(len(bus.control_calls), 1)
        self.assertEqual(bus.phase, "confirm_pending")
        self.assertIn("confirm=true", reply["result"]["content"][0]["text"])

    def test_off_from_any_phase_never_keys_and_needs_no_confirm(self):
        for start in ("rx", "confirm_pending", "tx"):
            bus = StubBus()
            bus._set_phase(start)
            reply, _ = call_tool("ptt", {"action": "off"}, bus)
            payload = reply["result"]["structuredContent"]
            self.assertFalse(reply["result"]["isError"], start)
            self.assertEqual(payload["ptt_phase"], "rx", start)
            self.assertFalse(payload["transmitting"], start)
            self.assertEqual(bus.control_calls, [("ptt_off", {})], start)

    def test_off_rejects_confirm_flag(self):
        reply, bus = call_tool("ptt", {"action": "off", "confirm": True})
        self.assertTrue(reply["result"]["isError"])
        self.assertEqual(bus.control_calls, [])

    def test_non_amateur_frequency_blocks_tx_even_after_two_taps(self):
        bus = StubBus(tx_allowed=False)
        call_tool("ptt", {"action": "on"}, bus)
        reply, _ = call_tool("ptt", {"action": "on", "confirm": True}, bus)
        payload = reply["result"]["structuredContent"]
        self.assertFalse(reply["result"]["isError"])  # 调用本身成了，是门禁拦下了发射
        self.assertEqual(payload["ptt_phase"], "rx")
        self.assertFalse(payload["transmitting"])
        self.assertIn("业余段", " ".join(payload["warnings"]))

    def test_expired_confirm_window_reports_rearm_not_transmit(self):
        bus = StubBus(confirm_expires=True)
        call_tool("ptt", {"action": "on"}, bus)
        reply, _ = call_tool("ptt", {"action": "on", "confirm": True}, bus)
        payload = reply["result"]["structuredContent"]
        self.assertFalse(payload["transmitting"])
        self.assertEqual(payload["ptt_phase"], "confirm_pending")
        self.assertIn("过期", " ".join(payload["warnings"]))

    def test_old_bus_without_ptt_phase_is_refused(self):
        state = StubBus().state_value
        state.pop("ptt_phase")
        bus = StubBus(state=state)
        reply, _ = call_tool("ptt", {"action": "on"}, bus)
        self.assertTrue(reply["result"]["isError"])
        self.assertEqual(bus.control_calls, [])
        self.assertIn("ptt_phase", reply["result"]["content"][0]["text"])

    def test_bad_action_and_bad_confirm_type(self):
        for args in ({"action": "toggle"}, {"action": "on", "confirm": "yes"}, {}):
            reply, bus = call_tool("ptt", args)
            self.assertTrue(reply["result"]["isError"], args)
            self.assertEqual(bus.control_calls, [], args)

    def test_already_transmitting_is_refused_without_confirm_path(self):
        bus = StubBus()
        bus._set_phase("tx")
        reply, _ = call_tool("ptt", {"action": "on"}, bus)
        self.assertTrue(reply["result"]["isError"])
        self.assertEqual(bus.control_calls, [])


class TuneAndModeTests(unittest.TestCase):
    def test_tune_sends_one_control_call_and_reports_mhz(self):
        reply, bus = call_tool("tune", {"freq_hz": 7_100_000})
        payload = reply["result"]["structuredContent"]
        self.assertFalse(reply["result"]["isError"])
        self.assertTrue(payload["tuned"])
        self.assertEqual(payload["freq_mhz"], 7.1)
        self.assertEqual(bus.control_calls, [("tune", {"freq": 7_100_000})])

    def test_tune_rejection_is_a_tool_error(self):
        reply, bus = call_tool("tune", {"freq_hz": 20_000})
        self.assertTrue(reply["result"]["isError"])
        self.assertIn("拒绝", reply["result"]["content"][0]["text"])
        self.assertEqual(bus.state_value["freq_hz"], 14074000.0)

    def test_tune_rejects_bad_frequency_types(self):
        for bad in (0, -1, "7100000", True, 7.1):
            reply, bus = call_tool("tune", {"freq_hz": bad})
            self.assertTrue(reply["result"]["isError"], bad)
            self.assertEqual(bus.control_calls, [], bad)

    def test_set_mode_applies_and_normalizes(self):
        reply, bus = call_tool("set_mode", {"mode": " cw "})
        self.assertFalse(reply["result"]["isError"])
        self.assertEqual(reply["result"]["structuredContent"]["mode"], "CW")
        self.assertEqual(bus.control_calls, [("set_mode", {"mode": "cw"})])

    def test_set_mode_rejects_unknown_and_missing(self):
        for args in ({"mode": "NOPE"}, {}, {"mode": 3}):
            reply, bus = call_tool("set_mode", args)
            self.assertTrue(reply["result"]["isError"], args)


class QsoTests(unittest.TestCase):
    def test_log_qso_requires_a_callsign(self):
        for args in ({}, {"callsign": "   "}, {"callsign": 5}):
            reply, bus = call_tool("log_qso", args)
            self.assertTrue(reply["result"]["isError"], args)
            self.assertEqual(bus.qso_calls, [], args)

    def test_log_qso_stringifies_frequency_and_uppercases_mode(self):
        reply, bus = call_tool("log_qso", {"callsign": " bg1abc ", "freq_mhz": 14.27,
                                           "mode": "usb", "rst_sent": "59"})
        payload = reply["result"]["structuredContent"]
        self.assertFalse(reply["result"]["isError"])
        self.assertEqual(payload["count"], 1)
        # ADIF 的字段长度按字符串算，数字不转字符串会被总线静默丢掉
        self.assertEqual(bus.qso_calls, [{"callsign": "bg1abc", "freq_mhz": "14.27",
                                          "mode": "USB", "rst_sent": "59"}])
        self.assertIn("内存", payload["note"])

    def test_log_qso_drops_fields_the_exporter_cannot_write(self):
        reply, bus = call_tool("log_qso", {"callsign": "BG1ABC", "comment": "会被丢掉"})
        self.assertFalse(reply["result"]["isError"])
        self.assertEqual(bus.qso_calls, [{"callsign": "BG1ABC"}])

    def test_export_adif_returns_text_and_record_count(self):
        reply, bus = call_tool("export_adif")
        payload = reply["result"]["structuredContent"]
        self.assertFalse(reply["result"]["isError"])
        self.assertEqual(payload["records"], 1)
        self.assertIn("<CALL:6>BG1ABC", payload["adif"])
        self.assertEqual(bus.control_calls, [])


class StdioFramingTests(unittest.TestCase):
    def test_stdout_holds_only_protocol_frames(self):
        stdin = io.StringIO(
            json.dumps(rpc("initialize", {"protocolVersion": "2025-11-25"}, 1)) + "\n"
            + json.dumps({"jsonrpc": "2.0", "method": "notifications/initialized"}) + "\n"
            + "\n"
            + json.dumps(rpc("tools/list", {}, 2)) + "\n"
            + "not json at all\n"
            + json.dumps(rpc("tools/call", {"name": "get_state"}, 3)) + "\n"
        )
        stdout = io.StringIO()
        logs: list[str] = []
        server.serve_stdio(StubBus(), in_stream=stdin, out_stream=stdout,
                           log=logs.append)
        lines = [ln for ln in stdout.getvalue().splitlines() if ln.strip()]
        self.assertEqual(len(lines), 4)  # 3 个请求 + 1 个坏帧，通知与空行不回话
        ids = [json.loads(ln).get("id") for ln in lines]
        self.assertEqual(ids, [1, 2, None, 3])
        self.assertEqual(json.loads(lines[2])["error"]["code"], server.PARSE_ERROR)
        self.assertTrue(logs)
        self.assertTrue(all("mcp_radio" not in ln for ln in lines))  # 日志没漏进 stdout

    def test_main_rejects_bad_config_on_stderr(self):
        # 本机万一设了 RADIO_BUS_TOKEN，这条会变绿得没意义，先摘掉再测
        saved = os.environ.pop("RADIO_BUS_TOKEN", None)
        old_err, old_out = sys.stderr, sys.stdout
        sys.stderr, sys.stdout = io.StringIO(), io.StringIO()
        try:
            code = server.main(["--url", "http://10.0.0.5:8765"])
            captured_err, captured_out = sys.stderr.getvalue(), sys.stdout.getvalue()
        finally:
            sys.stderr, sys.stdout = old_err, old_out
            if saved is not None:
                os.environ["RADIO_BUS_TOKEN"] = saved
        self.assertEqual(code, 2)
        self.assertIn("RADIO_BUS_TOKEN", captured_err)
        self.assertEqual(captured_out, "")  # 配置错误也不许碰 stdout


class SubprocessTransportTests(unittest.TestCase):
    """真起子进程走一遍 stdio —— Windows 的代码页坑只有这条路能抓到。"""

    def test_frames_are_utf8_over_a_pipe(self):
        tools_dir = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
        env = dict(os.environ, PYTHONPATH=tools_dir)
        env.pop("RADIO_BUS_TOKEN", None)
        proc = subprocess.Popen(
            [sys.executable, "-m", "mcp_radio", "--url", "http://127.0.0.1:1"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            env=env, cwd=tools_dir)
        request = json.dumps({"jsonrpc": "2.0", "id": 1, "method": "tools/list"}) + "\n"
        out, err = proc.communicate(request.encode("utf-8"), timeout=60)
        self.assertEqual(proc.returncode, 0, err.decode("utf-8", "replace"))
        frames = [ln for ln in out.decode("utf-8").splitlines() if ln.strip()]
        self.assertEqual(len(frames), 1, frames)
        tools = json.loads(frames[0])["result"]["tools"]
        by_name = {t["name"]: t for t in tools}
        self.assertIn("音频带", by_name["get_spectrum"]["description"])
        # stderr 允许有日志，但不能有协议帧：两边不能串
        self.assertNotIn('"jsonrpc"', err.decode("utf-8", "replace"))


if __name__ == "__main__":
    unittest.main()
