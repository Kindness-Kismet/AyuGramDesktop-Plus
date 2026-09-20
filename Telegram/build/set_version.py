#!/usr/bin/env python3
"""转发到本项目唯一的版本生成器，避免旧脚本继续解释第四段为 alpha。"""

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

from build_support.version import main


if __name__ == "__main__":
    sys.dont_write_bytecode = True
    main()
