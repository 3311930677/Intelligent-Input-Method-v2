# P3 阶段启动单：智能模型、设置中心与插件框架基础

- 状态：P3A 进行中
- 日期：2026-07-31
- 前置门禁：P2 已通过完整词库 Windows 11 TSF 验收并完成卸载清理

## 实施顺序

1. **P3A 模型基础**：版本化模型协议、ModelHost、可替换后端接口、确定性 Mock、异步调度、取消、超时与基础候选降级。
2. **P3B 设置中心**：配置 Schema、原子持久化与热加载，再接入 WinUI 3 设置界面。
3. **P3C 插件基础**：在确认容器格式、签名、信任链和安装目录后，实现 PluginHost、manifest、权限检查与进程插件生命周期。

该顺序让模型与配置先通过 CLI/测试壳稳定契约，再引入 UI 和第三方执行边界。

## P3A 首个纵向切片

- 定义 `ICandidateRanker`、`ITextCompletionModel`、`IAdvancedReranker`、`IModelBackend` 与 `IModelScheduler` 的最小内部接口。
- 建立独立 ModelHost 进程及版本化 IPC 消息，不把模型运行时加载进 TSF 或 Core Service。
- 确定性 Mock Backend 支持成功、延迟、取消、超时和故障注入。
- Core Service 先返回 P2 基础候选；智能结果只允许异步增量更新，模型不可用时保持原候选。
- 默认日志不记录完整用户输入，只记录请求 ID、模型 ID、耗时、状态和降级原因。

## P3A 门禁

- 无模型文件、无网络、ModelHost 未启动或崩溃时，P2 基础输入不回归。
- Mock 请求可取消且超时有界，不遗留子进程或阻塞命名管道。
- 同一输入和 Mock 配置产生确定性结果。
- Release 基准分别报告基础候选首次返回延迟和智能增量延迟。
- 接入真实模型前记录模型许可证、来源哈希、格式兼容性、包体、冷启动、峰值内存和 CPU/GPU 延迟。

## 暂不实施

- 不下载或打包 UER Chinese RoBERTa、MiniCPM4 或 Qwen3-Reranker。
- 不在 TSF 按键线程执行模型推理、磁盘读取或等待 ModelHost。
- 不在 P3A 冻结插件包容器、签名算法或证书信任链。

## P3A 验证记录

- 2026-07-31：新增独立 `owo_model` 静态库，定义候选排序、文本联想、高级重排、模型后端和调度器的最小可替换接口；未链接到 TSF。
- 2026-07-31：确定性 Mock Backend 按字典序重排候选，支持可控延迟和故障注入。异步调度器使用请求级停止令牌，覆盖成功、取消、超时和后端错误；Release CTest 14/14 通过。
- 2026-08-01：定义独立 ModelHost v1 二进制消息格式，包含魔数、协议版本、消息类型、状态、请求 ID、时间预算、模型 ID、输入、候选和诊断。严格解码拒绝未知版本、非法枚举、超限候选及尾随数据；Release CTest 15/15 通过。
- 2026-08-01：新增独立 `owo_model_host.exe` 与 `owo_model_shell.exe`，通过专用 v1 命名管道运行 Mock 排序。真实子进程验证中文 UTF-8 候选、确定性排序、协议关停、100 ms 超时和宿主缺失错误；完整 Release 构建及 CTest 15/15 通过。
- 2026-08-01：内部 Core 协议升级为 v3，增加 `candidate_update_request/response` 和 `model_pending`。`--model-host` 模式下基础候选实测 14 ms 返回，25 ms Mock 完成后按同一 request/generation 获取独立重排；宿主缺失降级为空增量。后台请求使用 5 秒回收、128 项硬上限，满载时不提交模型请求。
- 下一切片：让 TSF 在不阻塞按键线程的前提下轮询增量结果，并用 generation 校验丢弃过期更新。
