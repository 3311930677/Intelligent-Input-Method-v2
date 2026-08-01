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
- 2026-08-01：TSF 后台工作线程在基础候选标记 `model_pending` 时最多轮询 6 次、间隔 10 ms；新候选请求或提交反馈会立即中断轮询。增量结果必须同时匹配活动 request ID、generation 和页码，且不覆盖基础分页状态。Release CTest 15/15、Core 合约与子进程隔离回归通过。
- 2026-08-01：真实 Windows 11 记事本门禁通过。输入 `xian` 时先显示基础顺序“西安 / 先 / 线”，约 40 ms Mock 增量到达后更新为“先 / 线 / 西安”；随后快速把 `xian` 改为 `nihao`，旧 generation 未回写覆盖新输入。测试后 Core Service 与 ModelHost 均通过协议关停，TSF 配置已禁用并注销，COM 注册及后台进程无残留。
- 2026-08-01：完成 UER Chinese RoBERTa L4-H256 下载前预评估，详见 ADR-0005。该检查点是预训练 MLM 而非候选排序器，缺少排序任务头；模型仓库未声明权重许可证，不能用 UER-py 代码仓库的 Apache-2.0 自动覆盖权重及语料。当前仅列为“技术候选，许可阻塞”，未下载、转换或引入运行时依赖。
- 2026-08-01：新增 ModelHost 内部模型 manifest v1 校验与无依赖 WordPiece tokenizer。manifest 对完整来源 commit、模型/词表 SHA-256、显式许可、架构、任务、ONNX 格式、序列和候选上限失败封闭；tokenizer 严格校验 UTF-8，支持中文、ASCII 子词和特殊 token，超长输入不静默截断。Release 模型目标构建通过，CTest 16/16、Core 合约和子进程隔离回归通过；完整构建仅因 Windows 宿主仍占用既有 `OwO.TSF.dll` 而未完成 TSF 重链接。
- 2026-08-01：ModelHost 新增 `--asset-manifest` 可选启动参数，在创建命名管道前严格解析 v1 manifest，以 Windows CNG 固定缓冲流式校验模型和词表 SHA-256，并拒绝绝对路径、目录穿越及通过重解析目标逃逸 manifest 目录。合法合成资产的真实子进程启动与协议关停通过，错误哈希在监听前退出；Release 相关目标、CTest 17/17、Core 合约和子进程隔离回归通过。资产校验成功后仍明确使用 Mock，不冒充真实推理。
- 2026-08-01：新增可替换 `IInferenceSession`、固定形状推理批次和 `AssetCandidateRanker`。BERT 文本对严格编码为 `[CLS] context [SEP] candidate [SEP]`，同时生成 `input_ids`、`attention_mask` 与 `token_type_ids`；按 manifest 限制最多 8 个候选、序列长度 64，并用真实 `[PAD]` ID 补齐。合成 session 支持确定性分数、执行期取消、有界超时、故障注入及非有限分数拒绝。ModelHost 仅在同时指定 `--asset-manifest --synthetic-session` 时启用该路径，默认仍为 Mock。Release CTest 18/18、Core 合约和进程隔离回归通过；真实进程中文批处理排序与协议关停通过。
- 2026-08-01：冻结内部 ONNX 排序适配契约：默认 opset 17，允许范围 13～20；三个 `int64` 输入为 `input_ids`、`attention_mask`、`token_type_ids`，形状 `[dynamic_batch, 64]`；单个 `float32 logits` 输出形状 `[dynamic_batch, 1]`。manifest 明确记录全部名称、类型和维度约束，独立元数据比较器拒绝缺失/额外张量、名称或类型不符、动态 sequence、opset 不符及非排序任务。原始 `masked-lm` 资产不能进入 ModelHost 排序路径。Release CTest 18/18 通过，未引入 ONNX Runtime 或真实模型。
- 下一步：评估并冻结 ONNX Runtime CPU 的具体依赖版本、二进制体积、许可证和部署方式；未经许可仍不下载真实模型。
