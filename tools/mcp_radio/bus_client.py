#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""bus_client — radio-suite 总线（`apps/desktop/bus`）的只读薄封装.

存在的理由只有一条：MCP 侧不许长出自己的电台状态。所有真话都在 C++ 的
`RadioSession` 里，这里只把 `/api/*` 念出来。所以本模块：

- 纯标准库（urllib），没有 requests/httpx —— 宿主把适配器当子进程拉起时不该再装一堆包；
- 只读（GET）。写操作要等 M2，且必须继续走总线的 `/api/control` 两击门禁；
- token 只从环境变量读，只放请求头，绝不进 URL（URL 会落进 shell 历史、代理日志和
  `ps`），也绝不出现在异常文本里。
"""
from __future__ import annotations

import json
import os
import re
from typing import Any
from urllib.error import HTTPError, URLError
from urllib.parse import urlparse
from urllib.request import Request, urlopen

DEFAULT_PORT = 8765  # 与 apps/desktop/app/main.cpp 的 --bus 默认端口同源
TOKEN_HEADER = "X-Radio-Token"
LOOPBACK_HOSTS = {"127.0.0.1", "localhost", "::1"}


class BusError(RuntimeError):
    """总线契约层面的失败：调用方（MCP 工具）把它翻译成 isError 结果。"""


class BusConfigError(BusError):
    """配置本身就违法（非环回没 token、URL 畸形）。"""


class BusUnavailable(BusError):
    """连不上：总线没起、端口不对、或者被防火墙拦了。"""


class BusUnauthorized(BusError):
    """401：token 缺失或不匹配。"""


def base_url_from_env(env: dict[str, str] = os.environ) -> str:
    """RADIO_BUS_URL 优先，其次 RADIO_BUS_PORT，最后 http://127.0.0.1:8765."""
    url = (env.get("RADIO_BUS_URL") or "").strip()
    if not url:
        port = (env.get("RADIO_BUS_PORT") or "").strip() or str(DEFAULT_PORT)
        if not re.fullmatch(r"[0-9]{1,5}", port):
            raise BusConfigError(f"RADIO_BUS_PORT 不是端口号：{port!r}")
        url = f"http://127.0.0.1:{port}"
    return url


class BusClient:
    def __init__(
        self,
        base_url: str | None = None,
        token: str | None = None,
        timeout_s: float | None = None,
        env: dict[str, str] | None = None,
    ) -> None:
        env = os.environ if env is None else env
        self.base_url = (base_url or base_url_from_env(env)).rstrip("/")
        parts = urlparse(self.base_url)
        if parts.scheme != "http" or not parts.hostname:
            # 总线自己是明文 HTTP（局域网内），适配器不假装能升到 https，
            # 也不允许把 token 发给一个 https 都写不出来的地址。
            raise BusConfigError(f"总线地址必须是 http://host:port，收到 {self.base_url!r}")
        host = parts.hostname.lower()
        self.token = token if token is not None else (env.get("RADIO_BUS_TOKEN") or "").strip()
        if host not in LOOPBACK_HOSTS and not self.token:
            # 和 bus_server.cpp::start() 那条「非环回绑定必须带 token」同一条规矩，
            # 在客户端再挡一次：否则误配远端时会拿到一堆没有解释的 401。
            raise BusConfigError(
                f"总线在 {host}（非环回），必须设 RADIO_BUS_TOKEN 才允许连"
            )
        if timeout_s is None:
            timeout_s = float(env.get("RADIO_BUS_TIMEOUT_MS") or 2000) / 1000.0
        self.timeout_s = timeout_s

    def url_for(self, path: str) -> str:
        return self.base_url + path

    def get_json(self, path: str) -> Any:
        req = Request(self.url_for(path), method="GET")
        if self.token:
            req.add_header(TOKEN_HEADER, self.token)
        try:
            with urlopen(req, timeout=self.timeout_s) as resp:
                body = resp.read()
        except HTTPError as exc:
            if exc.code == 401:
                raise BusUnauthorized(
                    "总线返回 401：token 缺失或不匹配（用环境变量 RADIO_BUS_TOKEN 给）"
                ) from None
            raise BusError(f"总线 {path} 返回 HTTP {exc.code}") from None
        except URLError as exc:
            raise BusUnavailable(
                f"连不上总线 {self.base_url}：{exc.reason}. 先起 radio-suite --headless --bus"
            ) from None
        except OSError as exc:  # socket.timeout 在 Windows 上是 TimeoutError→OSError 家族
            raise BusUnavailable(f"读总线 {self.base_url} 超时/中断：{exc}") from None
        try:
            return json.loads(body.decode("utf-8"))
        except (ValueError, UnicodeDecodeError) as exc:
            raise BusError(f"总线 {path} 返回的不是合法 JSON：{type(exc).__name__}") from None

    # ---- 只读三面（M1）----

    def state(self) -> dict:
        data = self.get_json("/api/state")
        if not isinstance(data, dict):
            raise BusError("/api/state 返回的不是对象")
        return data

    def spectrum(self) -> dict:
        data = self.get_json("/api/spectrum")
        if not isinstance(data, dict) or not isinstance(data.get("power_db"), list):
            raise BusError("/api/spectrum 缺少 power_db 数组")
        return data
