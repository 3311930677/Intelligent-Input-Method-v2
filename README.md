# OwO Input Method

OwO 是一个面向 Windows 11、从零实现的模块化输入法项目。基础输入链路必须在 AI、插件和网络均不可用时继续工作。

当前处于 **P0：需求冻结与架构验证**。本阶段只建立可评审的架构基线和最小可构建探针，不提供可安装输入法。

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

