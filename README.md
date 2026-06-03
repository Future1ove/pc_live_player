# 直播流二维码快速检测器（C / C++）

## 程序说明

- **`douyin_qr_c`**：命令行 + SDL2 窗口，通过参数传入流地址（适合脚本、调试）。
- **`douyin_qr_gui`**：Qt6 图形界面，布局与样式对齐仓库根目录的 Python（PyQt5）版 `main.py`。

共享逻辑在静态库 **`douyin_core`** 中：FFmpeg 拉流、ZBar 检测、延迟统计等（见 `src/`）。

## 依赖（vcpkg 示例）

在 `c_version` 目录下使用 [vcpkg 清单模式](https://learn.microsoft.com/vcpkg/users/manifests) 时，`vcpkg.json` 已列出：`ffmpeg`、`sdl2`、`mchehab-zbar`、`libqrencode`、`qtbase`（含 `widgets` / `gui` 等特性）。

配置 CMake 时传入你的 vcpkg 工具链文件。本仓库已提供预设：若你的 vcpkg 在 `E:\Code\c\vcpkg`，可直接用 **CMake Presets**（`CMakePresets.json` 中 preset 名 `vcpkg-e-code-c`），或在命令行写死工具链路径，例如：

```powershell
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=E:/Code/c/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

首次安装 Qt6 等依赖时，vcpkg 编译时间较长，属正常现象。

## 运行

### GUI（推荐）

```powershell
# 将 vcpkg 的 bin 加入 PATH（FFmpeg/SDL2/Qt 等 DLL）
$env:PATH = "E:/Code/c/vcpkg/installed/x64-windows/bin;" + $env:PATH

./build/Release/douyin_qr_gui.exe
```

在窗口内粘贴直播流 URL，勾选 GPU / 全分辨率 / 音频后点击「开始检测」。

### 命令行（SDL）

```powershell
./build/Release/douyin_qr_c.exe --url 'https://example.com/live.flv'
```

参数说明见程序 `--help` 或 `app_print_usage`（与 `douyin_qr_c --help` 一致）。

## 部署 Qt GUI（Windows）

若双击 `douyin_qr_gui.exe` 提示缺少 `Qt6Core.dll` 等，请在构建环境中执行 `windeployqt`（或使用 vcpkg 的 `applocal` 依赖复制），将 Qt 平台插件与 DLL 放到 exe 同目录。开发阶段优先在已配置 PATH 的终端中运行。
