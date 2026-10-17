# Codex Proxy Switcher · Win32

C++17 + Windows 原生 Win32 控件，面向 Windows 10/11。静态链接 C/C++ 运行库；仍使用 Windows 自带系统 DLL，无需安装 .NET、WebView2 或 MinGW 运行库。

## 使用

将 `dist/CodexProxySwitcher.exe` 和 `dist/CodexProxySwitcher.ini` 一起放到可写文件夹。双击 EXE，输入本地 HTTP 代理端口，选择原生启动或代理启动。启动按钮会自动保存当前配置；成功启动后窗口关闭。

配置路径始终由 EXE 的实际位置确定，与命令行当前目录、快捷方式的起始位置无关。不会将配置写入 AppData。文件夹无写入权限时会明确报错；可将整个文件夹移动到用户有写入权限的位置。

### 代理启动快捷方式

在主界面填写端口和 Codex 路径后，点击“创建代理启动快捷方式”。程序会保存配置，并弹出保存位置选择框，默认选择桌面。

以后双击该快捷方式，会直接按 EXE 同目录的 INI 配置以代理模式启动，不创建启动器窗口，也不显示成功提示；遇到配置缺失、Codex 已运行或启动失败时才显示错误提示。需要修改设置时，直接双击 EXE 打开主界面。

快捷方式目标为当前 EXE，参数为 `--proxy`，图标取自 EXE 内嵌的蓝色 ChatGPT 标志。移动 EXE 后需要重新生成快捷方式；修改端口只需保存配置，无需重新生成。

```ini
[Launcher]
ProxyPort=7897
CodexPath=
```

`CodexPath` 留空时，通过 Windows 包管理接口查找当前用户的商店版 Codex，优先选择版本号最高的包，通过真实 AUMID 激活，兼容包内 Codex.exe 和新版 ChatGPT.exe。也可以浏览选择完整的程序路径。INI 使用 UTF-16 LE 编码，支持中文路径，可用记事本编辑。

代理启动设置 HTTP_PROXY、HTTPS_PROXY、ALL_PROXY 为 `http://127.0.0.1:端口`，NO_PROXY 为 `localhost,127.0.0.1,::1`，同时向桌面应用传入 `--proxy-server=http://127.0.0.1:端口`，让 Electron 界面请求也使用代理。商店激活和直接运行 EXE 均传递该参数，无需开启 Windows 系统代理。原生启动临时清除这些变量，不传入代理参数，界面仍遵循系统网络设置。

手动指定普通 EXE 时，仅调整启动器进程的环境，由子进程继承。商店应用激活时，按原项目的方法临时调整当前用户环境变量并广播环境变化；激活返回后恢复原来的值和注册表类型，错误退出路径同样执行恢复。此方法短时间内可能影响其他同时启动的应用；强制结束启动器或断电可能中断恢复。它不修改 Windows 系统代理。

如果 Codex 已运行，程序会提示先退出 Codex，避免 Electron 复用旧进程导致模式切换无效。不会自动强行终止现有会话。自动查找目前使用原项目的包族名 `OpenAI.Codex_2p2nqsd0c76g0`；包族名变化时需更新代码。

## 构建

MinGW-w64（g++ 和 windres 已在 PATH）：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1
```

输出到 `dist`。重复构建不会覆盖用户已有的 INI。

Visual Studio C++ 工具链：

```powershell
cmake -S . -B build
cmake --build build --config Release
```

MSVC 使用 `/MT` 静态运行库。该方式启动时会在 EXE 同目录创建默认 INI。

`CodexProxySwitcher.exe --self-test` 检查端口边界、中文配置往返读写、进程环境恢复、实际快捷方式的目标/参数/图标/工作目录及嵌入图标资源；退出码 0 表示通过。测试使用临时文件，不启动 Codex、不修改用户级环境变量或创建桌面快捷方式。

`--diagnose` 将自动查找的 Codex 路径与 AppUserModelID 输出到标准输出，供命令行诊断使用；不启动 Codex。

## 来源

功能参考 [hloolx/codex-proxy-switcher-win](https://github.com/hloolx/codex-proxy-switcher-win)，此目录是独立的原生 C++ 实现。`original-Program.cs.txt` 仅用于核对原实现，分发仅需 EXE 和 INI。

图标使用 [ChatGPT SVG 图形](https://commons.wikimedia.org/wiki/File:ChatGPT_logo.svg) 的几何路径，移除底色，将图形颜色改为蓝色 `#2563EB`。矢量源文件及多尺寸 ICO 位于 `assets`；可使用 `powershell -NoProfile -STA -ExecutionPolicy Bypass -File tools/make-icon.ps1` 重新生成，运行程序本身不依赖此工具。
