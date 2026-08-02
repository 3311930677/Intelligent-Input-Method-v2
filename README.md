# OwO Input Method

OwO 是一个面向 Windows 11、从零实现的模块化输入法项目。基础输入链路必须在 AI、插件和网络均不可用时继续工作。

本项目采用 `GPL-3.0-only` 许可证，完整条款见 `LICENSE`。选择该许可证是为了允许在遵守同一强 Copyleft 分发义务的前提下集成雾凇拼音词库派生数据。

当前处于 **P3C：插件框架基础**。P1 的 Windows 11 TSF—Core Service 闭环、P2 基础输入引擎、P3A 模型基础和 P3B 设置中心均已通过阶段验收；P3C 已完成受信包安装、版本绑定、零能力 AppContainer、安全管道、真实已安装入口的授权调用与回收、设置中心显式管理与精确版本卸载，以及 Core Service 的有界非 TSF 插件执行线程。可信用户动作入口与最终验收仍在推进。

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

基准也可接收一个已编译 `.owolx` 路径，对真实规模词库执行同一查询矩阵。

雾凇无注音词表使用锁定版本的 `8105.dict.yaml` 自动注音。读音权重达到主读音 5% 的分支均会生成，单条词目最多展开 64 种组合；未知字符或超限时导入失败。各词表编译后按 `8105 → base → ext → tencent → others` 的上游优先级合并：

```powershell
./build/windows-release/Release/owo_lexicon_compiler.exe `
  ./upstream/tencent.dict.yaml ./data/rime-ice-tencent-2026.06.30.manifest `
  ./build/tencent.owolx ./upstream/8105.dict.yaml
./build/windows-release/Release/owo_lexicon_merge.exe `
  ./build/rime-ice-cn.owolx ./build/8105.owolx ./build/base.owolx `
  ./build/ext.owolx ./build/tencent.owolx ./build/others.owolx
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

Core 默认热加载 `%LOCALAPPDATA%\OwO\InputMethod\config\owo.conf`，与设置中心共享配置。测试或诊断需要忽略用户设置时可传 `--no-config`；指定其他文件则使用 `--config <path>`。

### 设置中心（P3B）

设置中心使用 .NET 10 与 WinUI 3，SDK 版本由 `global.json` 锁定。先构建 C++ 配置后端，再构建 UI：

```powershell
.\scripts\build_settings_center.ps1 -Configuration Release
```

开发运行时可设置 `OWO_CONFIG_SHELL_PATH` 指向 `owo_config_shell.exe`，并用 `OWO_CONFIG_PATH` 覆盖默认的 `%LOCALAPPDATA%\OwO\InputMethod\config\owo.conf`。插件管理后端可用 `OWO_PLUGIN_SHELL_PATH` 覆盖，测试仓库可用 `OWO_PLUGIN_STORE_PATH` 覆盖默认的 `%LOCALAPPDATA%\OwO\InputMethod\plugins`。设置中心缺失不影响输入主链路。

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

P3A Mock ModelHost 使用独立 v1 管道和测试壳：

```powershell
./build/windows-release/Release/owo_model_host.exe
# 另一终端：
./build/windows-release/Release/owo_model_shell.exe nihao 泥号 你好
./build/windows-release/Release/owo_model_shell.exe --shutdown
```

可用 `owo_model_host.exe --latency-ms 200` 验证 100 ms 客户端预算下的明确超时，或使用 `--fail` 注入后端错误。ModelHost 缺失时测试壳返回 transport unavailable；这些错误不会改变 P2 基础候选。

Core Service 仅在显式传入 `--model-host` 时启用增量模型适配。候选请求立即返回基础结果和 `model_pending`；测试壳可用 `--update` 按同一 request/generation 获取完成的增量结果。后台表具有 5 秒回收和 128 项硬上限，满载时直接保留基础候选。

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
