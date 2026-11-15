#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""mcp_radio.server — 把本仓 HTTP 总线转成一个 MCP server（stdio，零第三方依赖）.

为什么不学 rsba1-core 用 fastmcp：那份代码已经有 Python 栈，我们这份只做转写，
`urllib` + `json` 够了；少一个依赖就少一次「宿主环境里 fastmcp 版本又漂了」的排查。
代价是 JSON-RPC 的骨架自己写 —— 但它就这几个方法，而且每条都有用例兜着。

协议按 2026-07-28 那一版：无状态、没有 Mcp-Session-Id、HTTP+SSE 传输已废弃，
所以这里只有 stdio。`initialize` 仍然回话（旧客户端还要它），但状态不依赖它。
"""
from __future__ import annotations

import argparse
import json
import sys
import time
from typing import Any, Callable

try:  # 包内导入（python -m mcp_radio）与裸脚本导入（直接跑文件）都要能用
    from .bus_client import BusClient, BusConfigError, BusError
except ImportError:  # pragma: no cover - 取决于调用方式
    from bus_client import BusClient, BusConfigError, BusError  # type: ignore

SERVER_NAME = "radio-suite"
SERVER_VERSION = "0.2.0"
LATEST_PROTOCOL = "2026-07-28"
SUPPORTED_PROTOCOLS = ("2025-06-18", "2025-11-25", LATEST_PROTOCOL)

# JSON-RPC 错误码（MCP 沿用）
PARSE_ERROR = -32700
INVALID_REQUEST = -32600
METHOD_NOT_FOUND = -32601
INVALID_PARAMS = -32602


TOOL_DOCS = {
    "get_state": (
        "读电台当前状态（频率/制式/滤波/PTT/S 表/ATT/NB/NR）。数据就是总线 /api/state 的字段，"
        "另加 freq_mhz 与 state_age_ms（后者假设总线与适配器同机；跨机时它是时钟偏差的函数）。"
    ),
    "get_spectrum": (
        "读一帧频谱快照。⚠️ 横轴是**解调后的音频带 0..fs/2**（bin_hz×bin 序号），不是 RF： "
        "瀑布/FFT 看到的是当前制式解调出来的基带，点它调谐只在 SSB 上有单值含义。"
        "默认把 1025 个 bin 折叠成最多 max_points 个峰值点（按桶取最大，保峰不保形），"
        "并给出 peak_hz / peak_db / noise_floor_db，别把整表塞回模型。"
    ),
    "link_health": (
        "自证这条链能看多远：总线可达？token 有效？频谱帧号在推进？状态多久没更新？PTT 在哪个方向？"
        "注意 `spectrum_advancing` 只证明总线自己的 DSP 定时器活着，"
        "**不证明电台链路活着**（真链路的活性判定在会话层，还没进总线，见 README）。"
    ),
    "tune": (
        "设频率（Hz，整数）。走总线 /api/control 的 tune，门禁在总线侧："
        "接收范围外直接拒绝；发射中把频率挪出业余段也拒绝。被拒时返回 isError，电台频率不变。"
    ),
    "set_mode": (
        "设制式：LSB/USB/AM/CW/RTTY/FM/WFM。名字不认识时总线回 bad mode，这里转成 isError。"
    ),
    "ptt": (
        "发射开关，**两击门禁**。action='on' 且不带 confirm：只武装（confirm_pending），不发射；"
        "action='on' 且 confirm=true：仅当门禁已处于待确认时才按下第二击进入 TX。"
        "一次调用永远只发一次总线动作，所以单次调用不可能从 RX 直接发射。"
        "action='off'：直接回 RX（不借道 TX，也不需要 confirm）。"
        "非业余段即使两击齐了也会被总线拦下，结果里会写明。"
    ),
    "log_qso": (
        "记一条 QSO 到总线的内存队列。callsign 必填；freq_mhz/mode/rst_sent 可选，"
        "也只有这三项会进 ADIF 导出。⚠️ 队列不落盘，进程重启即失。"
    ),
    "export_adif": (
        "取总线内存队列的 ADIF 文本（只读）。这不是磁盘上的 HamLog 库，"
        "要长期留存请自己把文本写进文件。"
    ),
}


def _fold_peaks(values: list, max_points: int) -> list:
    """按桶取最大把长谱压到 max_points 个点：agent 要的是「哪里有峰」，不是 1025 个浮点。"""
    n = len(values)
    if n == 0 or max_points >= n:
        return [round(float(v), 2) for v in values]
    out = []
    for i in range(max_points):
        lo = (i * n) // max_points
        hi = max(lo + 1, ((i + 1) * n) // max_points)
        bucket = values[lo:hi]
        out.append(round(float(max(bucket)), 2))
    return out


def _tool_get_state(bus: BusClient, args: dict, now_ms: Callable[[], int]) -> dict:
    st = bus.state()
    age = None
    last = st.get("last_update_ms")
    if isinstance(last, (int, float)) and last > 0:
        age = now_ms() - int(last)
    out = dict(st)
    out["freq_mhz"] = round(float(st.get("freq_hz", 0)) / 1e6, 6)
    out["state_age_ms"] = age
    return out


def _tool_get_spectrum(bus: BusClient, args: dict, now_ms: Callable[[], int]) -> dict:
    raw = bus.spectrum()
    max_points = args.get("max_points", 64)
    if not isinstance(max_points, int) or isinstance(max_points, bool):
        raise ValueError("max_points 必须是整数")
    if not 1 <= max_points <= 512:
        raise ValueError("max_points 要在 1..512 之间")
    db = [float(v) for v in raw["power_db"]]
    bin_hz = float(raw.get("bin_hz") or 0.0)
    peak_i = max(range(len(db)), key=db.__getitem__) if db else 0
    floor = sorted(db)[len(db) // 10] if db else 0.0  # 10 分位当噪声底，比 min 稳
    return {
        "frame_seq": raw.get("frame_seq"),
        "bin_hz": bin_hz,
        "bin_count": len(db),
        "axis": {
            "kind": "audio_baseband_hz",
            "from_hz": 0.0,
            "to_hz": round(bin_hz * max(len(db) - 1, 0), 3),
            "note": "解调后的音频谱，不是 RF 频率；换算偏移只在 SSB 上有单值意义",
        },
        "peak_bin": peak_i,
        "peak_hz": round(bin_hz * peak_i, 1),
        "peak_db": round(db[peak_i], 2) if db else None,
        "noise_floor_db": round(floor, 2) if db else None,
        "max_points": max_points,
        "power_db_peaks": _fold_peaks(db, max_points),
    }


def _tool_link_health(bus: BusClient, args: dict, now_ms: Callable[[], int]) -> dict:
    window_ms = args.get("window_ms", 200)
    if not isinstance(window_ms, int) or isinstance(window_ms, bool):
        raise ValueError("window_ms 必须是整数")
    if not 0 <= window_ms <= 2000:
        raise ValueError("window_ms 要在 0..2000 之间")
    state = bus.state()
    first = bus.spectrum()
    # 速率要用实测的间隔算，不能拿 window_ms 当分母：window_ms=0 时那两次读其实
    # 隔了几十毫秒，写死成 1 ms 就把 3 帧报成 3000 fps —— 一个凭空的谎。
    mark = time.monotonic()
    if window_ms:
        time.sleep(window_ms / 1000.0)
    second = bus.spectrum()
    measured_ms = max(round((time.monotonic() - mark) * 1000), 1)
    seq_a = int(first.get("frame_seq") or 0)
    seq_b = int(second.get("frame_seq") or 0)
    delta = seq_b - seq_a
    last = state.get("last_update_ms")
    age = now_ms() - int(last) if isinstance(last, (int, float)) and last > 0 else None
    warnings = []
    if delta <= 0:
        warnings.append("频谱帧号在窗口内没推进：总线的采样定时器停了")
    if age is not None and age > 5000:
        warnings.append(f"电台状态已 {age} ms 没更新（总线与适配器需同机，否则这是时钟偏差）")
    if state.get("ptt_on"):
        warnings.append("当前在发射（PTT=TX）")
    return {
        "bus_url": bus.base_url,
        "bus_reachable": True,
        "token_configured": bool(bus.token),
        "spectrum_frame_seq_first": seq_a,
        "spectrum_frame_seq_second": seq_b,
        "spectrum_advancing": delta > 0,
        "frames_in_window": delta,
        "requested_window_ms": window_ms,
        "measured_window_ms": measured_ms,
        "approx_frames_per_s": round(delta * 1000.0 / measured_ms, 1) if delta > 0 else 0.0,
        "state_age_ms": age,
        "ptt_on": bool(state.get("ptt_on")),
        "mode": state.get("mode"),
        "freq_hz": state.get("freq_hz"),
        "verdict": "ok" if delta > 0 and not warnings else "degraded",
        "warnings": warnings,
        "scope_note": "这里能证明的只有总线自己；电台链路活性还没进总线，别把 ok 读成「电台通了」",
    }


def _tool_tune(bus: BusClient, args: dict, now_ms: Callable[[], int]) -> dict:
    hz = args.get("freq_hz")
    if isinstance(hz, bool) or not isinstance(hz, int):
        raise ValueError("freq_hz 必须是整数（Hz）")
    if hz <= 0:
        raise ValueError("freq_hz 必须为正")
    result = bus.control("tune", freq=hz)
    if not result.get("tuned"):
        # 拒绝不是「工具坏了」，但也不是成功：让模型读到 isError 才会停下来换频率，
        # 而不是把 tuned:false 当成「设好了」继续往下走。
        raise BusError(
            f"总线门禁拒绝设频 {hz} Hz（{result.get('error', 'rejected')}）；电台频率未改变"
        )
    return {
        "tuned": True,
        "freq_hz": hz,
        "freq_mhz": round(hz / 1e6, 6),
        "ptt_phase": bus.state().get("ptt_phase"),
    }


def _tool_set_mode(bus: BusClient, args: dict, now_ms: Callable[[], int]) -> dict:
    mode = args.get("mode")
    if not isinstance(mode, str) or not mode.strip():
        raise ValueError("mode 必填，取 LSB/USB/AM/CW/RTTY/FM/WFM")
    result = bus.control("set_mode", mode=mode.strip())
    return {"mode": result.get("mode"), "requested": mode.strip()}


def _tool_ptt(bus: BusClient, args: dict, now_ms: Callable[[], int]) -> dict:
    """一次调用 = 一次总线动作，绝不在封装里连发两击。

    「武装」和「确认」必须是两次独立的工具调用，且确认那次要显式带 confirm=true。
    判断依据是总线 /api/state 的 ptt_phase（门禁相位），不是 ptt_on（电台方向）——
    后者在 confirm_pending 时仍是 false，拿它判会把「已武装」读成「空闲」。
    """
    action = args.get("action")
    if action not in ("on", "off"):
        raise ValueError("action 只能是 'on' 或 'off'")
    confirm = args.get("confirm", False)
    if not isinstance(confirm, bool):
        raise ValueError("confirm 必须是布尔值")

    if action == "off":
        if confirm:
            raise ValueError("关发射不需要 confirm：它随时允许，且不会借道 TX")
        result = bus.control("ptt_off")
        phase = result.get("ptt")
        return {
            "action": "off",
            "ptt_phase": phase,
            "transmitting": phase == "tx",
            "note": "走总线的直接回 RX，不经过待确认，也不会把 RX 按成发射",
        }

    state = bus.state()
    if "ptt_phase" not in state:
        raise BusError(
            "总线 /api/state 没有 ptt_phase 字段：这个总线版本太旧，"
            "无法判断门禁是否已武装，拒绝执行 PTT（先更新 radio-suite）"
        )
    before = state["ptt_phase"]
    if confirm:
        if before != "confirm_pending":
            raise BusError(
                f"门禁当前是 {before}，不是待确认：confirm=true 只用于第二击。"
                "先不带 confirm 调一次武装，再带 confirm=true 确认。"
            )
    else:
        if before == "confirm_pending":
            raise BusError(
                "门禁已武装（待确认）。要发射请显式带 confirm=true；要取消请用 action='off'。"
            )
        if before == "tx":
            raise BusError("已经在发射。要关闭请用 action='off'（不带 confirm）。")

    result = bus.control("ptt_on")  # 恰好一次按键
    phase = result.get("ptt")
    warnings = []
    if confirm and phase == "confirm_pending":
        warnings.append("确认窗已过期：这一下只是重新武装，没有发射")
    elif confirm and phase == "rx":
        warnings.append("发射被门禁拦下：当前频率可收听但不在业余段")
    if phase == "confirm_pending":
        next_step = "已武装但未发射。要发射：在确认窗内带 confirm=true 再调一次；要取消：action='off'"
    elif phase == "tx":
        next_step = "正在发射。关闭用 action='off'"
    else:
        next_step = "未发射。要发射需先不带 confirm 武装，再带 confirm=true 确认"
    return {
        "action": "on",
        "confirm": confirm,
        "phase_before": before,
        "ptt_phase": phase,
        "transmitting": phase == "tx",
        "bus_presses_sent": 1,
        "next_step": next_step,
        "warnings": warnings,
    }


QSO_TEXT_FIELDS = ("freq_mhz", "mode", "rst_sent")


def _tool_log_qso(bus: BusClient, args: dict, now_ms: Callable[[], int]) -> dict:
    callsign = args.get("callsign")
    if not isinstance(callsign, str) or not callsign.strip():
        raise ValueError("callsign 必填且不能是空白")
    qso = {"callsign": callsign.strip()}
    for key in QSO_TEXT_FIELDS:
        if key not in args:
            continue
        value = args[key]
        if key == "freq_mhz":
            if isinstance(value, bool) or not isinstance(value, (str, int, float)):
                raise ValueError("freq_mhz 必须是字符串或数字")
            qso[key] = value if isinstance(value, str) else str(value)
        else:
            if not isinstance(value, str):
                raise ValueError(f"{key} 必须是字符串")
            qso[key] = value.strip().upper() if key == "mode" else value.strip()
    reply = bus.log_qso(qso)
    return {
        "logged": True,
        "count": reply.get("count"),
        "qso": qso,
        "note": "总线只在内存里排队，进程重启即失；要留存请调 export_adif 取文本",
    }


def _tool_export_adif(bus: BusClient, args: dict, now_ms: Callable[[], int]) -> dict:
    text = bus.qso_adif()
    return {
        "adif": text,
        "records": text.count("<EOR>"),
        "note": "这是总线内存队列的导出，不是磁盘上的 HamLog 库",
    }


TOOLS = {
    "get_state": _tool_get_state,
    "get_spectrum": _tool_get_spectrum,
    "link_health": _tool_link_health,
    "tune": _tool_tune,
    "set_mode": _tool_set_mode,
    "ptt": _tool_ptt,
    "log_qso": _tool_log_qso,
    "export_adif": _tool_export_adif,
}

# 写工具不能带 readOnlyHint。ptt 另标 destructiveHint：它能把发射机推上空。
TOOL_ANNOTATIONS = {
    "get_state": {"readOnlyHint": True},
    "get_spectrum": {"readOnlyHint": True},
    "link_health": {"readOnlyHint": True},
    "tune": {"readOnlyHint": False, "destructiveHint": False, "openWorldHint": True},
    "set_mode": {"readOnlyHint": False, "destructiveHint": False, "openWorldHint": True},
    "ptt": {"readOnlyHint": False, "destructiveHint": True, "openWorldHint": True},
    "log_qso": {"readOnlyHint": False, "destructiveHint": False, "idempotentHint": False},
    "export_adif": {"readOnlyHint": True},
}


def tool_definitions() -> dict:
    empty = {"type": "object", "additionalProperties": False, "properties": {}}
    spectrum_schema = {
        "type": "object",
        "additionalProperties": False,
        "properties": {
            "max_points": {"type": "integer", "minimum": 1, "maximum": 512, "default": 64},
        },
    }
    health_schema = {
        "type": "object",
        "additionalProperties": False,
        "properties": {
            "window_ms": {"type": "integer", "minimum": 0, "maximum": 2000, "default": 200},
        },
    }
    tune_schema = {
        "type": "object",
        "additionalProperties": False,
        "required": ["freq_hz"],
        "properties": {
            "freq_hz": {"type": "integer", "minimum": 1, "description": "目标频率，单位 Hz"},
        },
    }
    mode_schema = {
        "type": "object",
        "additionalProperties": False,
        "required": ["mode"],
        "properties": {
            "mode": {"type": "string", "enum": ["LSB", "USB", "AM", "CW", "RTTY", "FM", "WFM"]},
        },
    }
    ptt_schema = {
        "type": "object",
        "additionalProperties": False,
        "required": ["action"],
        "properties": {
            "action": {"type": "string", "enum": ["on", "off"]},
            "confirm": {
                "type": "boolean",
                "default": False,
                "description": "仅 action='on' 有效：显式确认第二击。门禁未武装时带它会报错，不会替你武装",
            },
        },
    }
    qso_schema = {
        "type": "object",
        "additionalProperties": False,
        "required": ["callsign"],
        "properties": {
            "callsign": {"type": "string", "minLength": 1},
            "freq_mhz": {"type": ["string", "number"]},
            "mode": {"type": "string"},
            "rst_sent": {"type": "string"},
        },
    }
    schemas = {
        "get_state": empty,
        "get_spectrum": spectrum_schema,
        "link_health": health_schema,
        "tune": tune_schema,
        "set_mode": mode_schema,
        "ptt": ptt_schema,
        "log_qso": qso_schema,
        "export_adif": empty,
    }
    return {
        "tools": [
            {
                "name": name,
                "description": TOOL_DOCS[name],
                "inputSchema": schemas[name],
                "annotations": {"title": name, **TOOL_ANNOTATIONS[name]},
            }
            for name in TOOLS
        ]
    }


def _result(req_id: Any, result: dict) -> dict:
    return {"jsonrpc": "2.0", "id": req_id, "result": result}


def _error(req_id: Any, code: int, message: str) -> dict:
    return {"jsonrpc": "2.0", "id": req_id, "error": {"code": code, "message": message}}


def dispatch(msg: dict, bus: BusClient, now_ms: Callable[[], int] = lambda: int(time.time() * 1000)) -> dict | None:
    """一条入站消息 → 一条应答（通知返回 None）。纯函数，用例直接打这里。"""
    if not isinstance(msg, dict) or msg.get("jsonrpc") != "2.0" or "method" not in msg:
        return _error(msg.get("id") if isinstance(msg, dict) else None,
                      INVALID_REQUEST, "不是 JSON-RPC 2.0 请求")
    method = msg["method"]
    req_id = msg.get("id")
    is_notification = "id" not in msg
    params = msg.get("params") or {}

    if method == "initialize":
        requested = params.get("protocolVersion")
        version = requested if requested in SUPPORTED_PROTOCOLS else LATEST_PROTOCOL
        if is_notification:
            return None
        return _result(req_id, {
            "protocolVersion": version,
            "capabilities": {"tools": {"listChanged": False}},
            "serverInfo": {"name": SERVER_NAME, "version": SERVER_VERSION},
            "instructions": "radio-suite 总线门面（M2：只读三工具 + 写工具 tune/set_mode/ptt/log_qso/export_adif）。"
                            "发射必须两击：ptt(action='on') 只武装，ptt(action='on', confirm=true) 才进 TX，"
                            "一次调用不可能从 RX 直接发射；非业余段由总线门禁拒绝。"
                            "QSO 队列只在内存里，要留存请用 export_adif 取文本。",
        })
    if is_notification:  # notifications/initialized、/cancelled 及以后新增的都吞掉
        return None
    if method == "ping":
        return _result(req_id, {})
    if method == "tools/list":
        return _result(req_id, tool_definitions())
    if method == "tools/call":
        name = params.get("name")
        if name not in TOOLS:
            return _error(req_id, INVALID_PARAMS, f"未知工具：{name!r}（可用：{sorted(TOOLS)}）")
        args = params.get("arguments") or {}
        if not isinstance(args, dict):
            return _error(req_id, INVALID_PARAMS, "arguments 必须是对象")
        try:
            payload = TOOLS[name](bus, args, now_ms)
        except (BusError, ValueError) as exc:
            # 工具层的失败按 MCP 的规矩回 isError 结果，而不是协议错误：
            # 模型要能读到「为什么没成」，才能自己决定下一步。
            return _result(req_id, {
                "content": [{"type": "text", "text": f"{type(exc).__name__}: {exc}"}],
                "isError": True,
            })
        text = json.dumps(payload, ensure_ascii=False, separators=(",", ":"))
        return _result(req_id, {
            "content": [{"type": "text", "text": text}],
            "structuredContent": payload,
            "isError": False,
        })
    return _error(req_id, METHOD_NOT_FOUND, f"不支持的方法：{method}")


def serve_stdio(bus: BusClient, in_stream=None, out_stream=None,
                log: Callable[[str], None] = lambda m: None) -> int:
    in_stream = in_stream or sys.stdin
    out_stream = out_stream or sys.stdout
    log(f"mcp_radio 起线程：stdio → {bus.base_url}")
    for line in in_stream:
        line = line.strip()
        if not line:
            continue
        try:
            msg = json.loads(line)
        except ValueError:
            out_stream.write(json.dumps(_error(None, PARSE_ERROR, "入站不是合法 JSON")) + "\n")
            out_stream.flush()
            continue
        try:
            reply = dispatch(msg, bus)
        except Exception as exc:  # 兜底：适配器绝不能把 traceback 吐进 stdout
            log(f"未预期异常：{type(exc).__name__}: {exc}")
            reply = _error(msg.get("id") if isinstance(msg, dict) else None,
                           -32603, "适配器内部错误（详见 stderr）")
        if reply is not None:
            out_stream.write(json.dumps(reply, ensure_ascii=False) + "\n")
            out_stream.flush()
    log("stdin 结束，适配器退出")
    return 0


def force_utf8_stdio() -> None:
    """stdio 的帧必须是 UTF-8。

    Windows 上 stdout 被重定向进管道时，Python 会按本地代码页（这台机器是 GBK）编码，
    于是中文工具描述先变成乱码、再变成对端解不开的协议帧 —— 单测用 StringIO
    抓不到，只有真起子进程才暴露。stdin 同理，读侧也必须按 UTF-8 解。
    """
    for stream in (sys.stdin, sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", newline="\n")
        except (AttributeError, ValueError, OSError):  # pragma: no cover - 被替换过的流
            pass


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        prog="mcp_radio",
        description="radio-suite 总线的 MCP 门面（stdio，M2 读写）。token 只认环境变量。",
    )
    ap.add_argument("--url", dest="base_url", default=None,
                    help="总线地址，默认取 RADIO_BUS_URL / RADIO_BUS_PORT / http://127.0.0.1:8765")
    ap.add_argument("--transport", choices=("stdio",), default="stdio",
                    help="只做 stdio：HTTP+SSE 传输已被 2026-07-28 版规范废弃，Streamable HTTP 是 M3")
    args = ap.parse_args(argv)
    force_utf8_stdio()
    try:
        bus = BusClient(base_url=args.base_url)
    except BusConfigError as exc:
        # 配置错误也写 stderr：stdout 的每一个字节都要留给协议帧
        print(f"mcp_radio: {exc}", file=sys.stderr)
        return 2
    return serve_stdio(bus, log=lambda m: print(f"mcp_radio: {m}", file=sys.stderr))


if __name__ == "__main__":
    sys.exit(main())
