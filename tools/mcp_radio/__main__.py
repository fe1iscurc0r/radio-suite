# SPDX-License-Identifier: AGPL-3.0-or-later
"""`python -m mcp_radio` 入口（stdio）."""
from __future__ import annotations

import sys

from .server import main

if __name__ == "__main__":
    sys.exit(main())
