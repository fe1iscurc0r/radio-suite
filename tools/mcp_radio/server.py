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
SERVER_VERSION = "0.1.0"
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
    if window_ms:
        time.sleep(window_ms / 1000.0)
    second = bus.spectrum()
    seq_a = int(first.get("frame_seq") or 0)
    seq_b = int(second.get("frame_seq") or 0)
    delta = seq_b - seq_a
    span_ms = window_ms or 1
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
        "window_ms": window_ms,
        "approx_frames_per_s": round(delta * 1000.0 / span_ms, 1) if delta > 0 else 0.0,
        "state_age_ms": age,
        "ptt_on": bool(state.get("ptt_on")),
        "mode": state.get("mode"),
        "freq_hz": state.get("freq_hz"),
        "verdict": "ok" if delta > 0 and not warnings else "degraded",
        "warnings": warnings,
        "scope_note": "这里能证明的只有总线自己；电台链路活性还没进总线，别把 ok 读成「电台通了」",
    }


TOOLS = {
    "get_state": _tool_get_state,
    "get_spectrum": _tool_get_spectrum,
    "link_health": _tool_link_health,
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
    schemas = {"get_state": empty, "get_spectrum": spectrum_schema, "link_health": health_schema}
    return {
        "tools": [
            {
                "name": name,
                "description": TOOL_DOCS[name],
                "inputSchema": schemas[name],
                # M1 三件全是只读；标注给宿主看，真正的门禁在总线侧（M2 的写工具不能带这个）
                "annotations": {"title": name, "readOnlyHint": True},
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
            "instructions": "radio-suite 总线门面（M1 只读三工具）。发射类工具尚未接入；"
                            "总线侧的 PTT 两击门禁与非业余段拒绝不会因为这里是只读而放松。",
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
            return _error(req_id, INVALID_PARAMS, f"未知工具：{name!r}（M1 只有 {sorted(TOOLS)}）")
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
        description="radio-suite 总线的 MCP 门面（stdio，只读 M1）。token 只认环境变量。",
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
