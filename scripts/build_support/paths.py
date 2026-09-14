from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

BUILD_DIR = ROOT / "build"
TMP_DIR = BUILD_DIR / "tmp"
LOGS_DIR = BUILD_DIR / "logs"

# 产物目录带版本号：build/<APP_NAME>-v<版本>-win-x64-<profile>
APP_NAME = "AyuGram"

# 依赖与中间产物一律收在 build/tmp，不外溢到仓库同级目录
LIBRARIES_DIR = TMP_DIR / "Libraries"
THIRD_PARTY_DIR = TMP_DIR / "ThirdParty"
CMAKE_OUT_DIR = TMP_DIR / "out"

# win64 目标的库前缀，与固化配方中的目录约定一致
LIBRARIES_ARCH_DIR = LIBRARIES_DIR / "win64"
USED_PREFIX_DIR = LIBRARIES_ARCH_DIR / "local"

# prebuild 建出的工具，codegen、d3d 校验和 NuGet 都从这里取
VENV_PYTHON = THIRD_PARTY_DIR / "python" / "Scripts" / "python.exe"
NUGET_EXE = THIRD_PARTY_DIR / "NuGet" / "nuget.exe"

VERSION_FILE = ROOT / "Telegram" / "build" / "version"

# 官方文档公开的测试用凭据，正式分发需自行申请
DEFAULT_API_ID = "2040"
DEFAULT_API_HASH = "b18441a1ff607e10a989891a5462e627"

# 构建产物，Updater 缺失不视为失败
PRODUCT_BINARIES = ("AyuGram.exe", "Updater.exe")

# 仅 Debug 收集，符号文件缺失不视为失败
DEBUG_SYMBOLS = ("AyuGram.pdb",)
