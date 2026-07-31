# OwO Input Method

OwO 是一个面向 Windows 11、从零实现的模块化输入法项目。基础输入链路必须在 AI、插件和网络均不可用时继续工作。

本项目采用 `GPL-3.0-only` 许可证，完整条款见 `LICENSE`。选择该许可证是为了允许在遵守同一强 Copyleft 分发义务的前提下集成雾凇拼音词库派生数据。

当前已进入 **P2：基础输入引擎 MVP**；P1 的 Windows 11 TSF—Core Service 最小闭环已通过验收。P2 将以输入方案抽象和全拼解析为起点，逐步接入可追溯词库、候选排序和用户词频；尚未形成可日常使用的输入法。

P1 TSF 原型捕获英文字母形成临时预编辑缓冲，在后台请求固定候选；`1` 或空格提交，Backspace 删除，Escape 取消。候选窗目前以鼠标位置作为尚未取得文本光标位置时的回退，不代表最终交互。

## 已确认边界

- 首要平台：Windows 11 x64；其他 Windows 版本后续适配。
- 语言与工具链：C++20、MSVC、CMake、vcpkg。
- Windows 输入入口：TSF/COM，不使用低级键盘钩子替代。
- `OwO.TSF.dll` 保持最小化；耗时 I/O、模型和第三方插件在宿主进程外执行。
- P1 可先用命令行和测试壳验证 TSF—Core Service 契约，再实现完整候选窗口。

## 配置、构建与测试

要求 Visual Studio 2022 Build Tools，并安装 MSVC x64 工具链。

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug
```

Release 构建：

```powershell
cmake --preset windows-release
cmake --build --preset windows-release
ctest --preset windows-release
```

架构文档位于 `docs/`。重要决策位于 `docs/adr/`。

Release 引擎基准：

```powershell
cmake --build --preset windows-release --target owo_engine_benchmark
./build/windows-release/Release/owo_engine_benchmark.exe
```

基准会输出 JSON 格式的样本数、夹具规模、p50/p95/p99 和最大延迟。历史报告位于 `docs/benchmarks/`。

## IPC 验证

先启动服务：

```powershell
./build/windows-debug/Debug/owo_core_service.exe
```

使用编译后词典时显式传入路径；文件缺失、损坏或版本不支持时服务拒绝启动：

```powershell
./build/windows-release/Release/owo_core_service.exe `
  --lexicon ./build/windows-release/runtime.owolx `
  --user-frequency ./build/windows-release/user-frequency.owuf
```

不传 `--lexicon` 时仅使用仓库内置的小型开发降级词典，不代表完整词库已集成。指定 `--user-frequency` 后，成功上屏的候选由 TSF 后台反馈给 Core Service；服务每累积 32 次选择或协议关停时原子落盘。

候选翻页支持 `PageUp`/`PageDown` 以及紧凑键盘上的 `[`/`]`。

在另一终端请求中文候选并有序关停：

```powershell
./build/windows-debug/Debug/owo_ipc_shell.exe nihao
./build/windows-debug/Debug/owo_ipc_shell.exe --shutdown
```

CTest 使用进程内专用管道契约测试，覆盖连续请求、中文候选、request/generation 回传和协议关停。Windows 真实子进程崩溃、重启、缺失服务和超时隔离由独立测试程序验证：

```powershell
./build/windows-release/Release/owo_core_service_contract.exe
./build/windows-release/Release/owo_ipc_integration.exe `
  ./build/windows-release/Release/owo_core_service.exe
```

两条命令退出码均为 `0` 表示进程内协议契约和子进程隔离矩阵通过。它们不由 CTest 直接启动，因为当前 Windows/CTest 组合在捕获命名管道测试时会等待到超时；CTest 与直接执行结果必须分开报告。

## 开发 TSF 注册

注册会修改当前用户的 COM 配置和系统 TSF 配置，需要在提升权限的 PowerShell 中运行；注册后的 OwO 配置默认禁用，不改变当前输入法。只用于本地开发验证，并提供对应逆操作：

```powershell
./scripts/register-dev.ps1 -Configuration Debug
./scripts/unregister-dev.ps1 -Configuration Debug
```

可用 TSF API 检查配置是否存在及当前激活状态：

```powershell
./build/windows-debug/Debug/owo_tsf_profile_check.exe
```

真实应用验收必须由 Windows 选择该配置，不能由测试程序伪造 TSF client ID：

```powershell
# 1. 提升权限注册；注册后默认禁用
./scripts/register-dev.ps1 -Configuration Release

# 2. 普通终端显式启用，然后使用 Win+Space 在测试应用中选择 OwO
./build/windows-release/Release/owo_tsf_profile_check.exe --enable

# 3. 验收结束立即恢复
./build/windows-release/Release/owo_tsf_profile_check.exe --disable
./scripts/unregister-dev.ps1 -Configuration Release
```

验收时先启动 `owo_core_service.exe`；输入字母后应显示固定候选，按 `1` 或空格应提交“固定候选”。
