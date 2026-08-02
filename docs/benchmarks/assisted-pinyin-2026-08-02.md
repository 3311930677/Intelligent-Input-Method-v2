# 不完整与纠错拼音候选基准

- 日期：2026-08-02
- 配置：MSVC x64 Release
- 系统：Windows 11，AMD Ryzen 9 9955HX，32 逻辑处理器
- 词库：雾凇拼音完整启用词表，1,885,739 条

## 方法

`owo_engine_benchmark` 继续以 100 次预热、1,000 次采样测量“解析、索引查词、Beam Search、排序”的同步引擎阶段。默认查询集保持 `nihao`、`zhongguo`、`shijie`、`ceshi`；新增 `--assisted` 查询集为 `b`、`nih`、`zhongg`、`niaho`。

```powershell
cmake --build --preset windows-release --target owo_engine_benchmark
build/windows-release/Release/owo_engine_benchmark.exe `
    build/windows-release/rime-ice-cn-2026.06.30.owolx
build/windows-release/Release/owo_engine_benchmark.exe `
    build/windows-release/rime-ice-cn-2026.06.30.owolx --assisted
```

两组均连续运行三轮。该数据不包含 IPC、TSF 绘制和首次词库加载。

## 结果

单位：微秒。

| 查询集 | 运行 | p50 | p95 | p99 | 最大值 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 精确 | 1 | 303.2 | 3426.7 | 3640.9 | 3864.2 |
| 精确 | 2 | 283.2 | 3137.7 | 3474.6 | 3588.3 |
| 精确 | 3 | 227.6 | 1666.4 | 2368.6 | 2473.5 |
| 辅助 | 1 | 2550.8 | 3312.0 | 4557.1 | 6280.4 |
| 辅助 | 2 | 2556.3 | 3522.1 | 3640.1 | 4406.0 |
| 辅助 | 3 | 2099.5 | 3263.4 | 3450.9 | 4069.0 |

按 p50 取中位运行，精确集为运行 2（p50 283.2 μs，p95 3137.7 μs），辅助集为运行 1（p50 2550.8 μs，p95 3312.0 μs）。两组每轮均返回 10,000 个候选。精确集三轮 p95 离散较大，因此同时保留每轮原始值，不以单轮结果掩盖系统调度波动。

## 回归判断

同一轮优化前的既有 Release 二进制单轮测得精确集 p50 198.7 μs、p95 1653.5 μs、p99 1734.8 μs。实现过程中曾因“对所有合法输入预生成纠错”出现 p50 6258.7 μs、p95 37004.5 μs 的明确回归；冻结为仅在无完整切分时纠错后，最终三轮精确集 p50 为 227.6～303.2 μs，已消除该数量级回归。辅助集因最多扩展 32 条路径而更慢，但三轮 p95 为 3.26～3.52 ms，仍在同步 Core 引擎的低毫秒范围内。
