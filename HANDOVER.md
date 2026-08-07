# OwO 输入法 — 换电脑交接文档

> 最后更新：2026-08-07 | 维护人：henrytan

---

## 1. 项目概述

### 仓库

| 项目 | 路径 / Remote | 角色 |
|------|--------------|------|
| **OwO-v2**（主体） | `d:\UGit\OwO-v2` / `3311930677/Intelligent-Input-Method-v2` | Windows C++ 输入法 |
| **灵犀** (LingXi) | `d:\UGit\cross-app-assistant` / `3311930677/LingXi-DesktopAgent` | Rust/Tauri v2，OwO 的 L3 桌面辅助插件 |
| **大创文档** | `d:\UGit\大创\` (不在 git) | 市级申报书 + 四份规划文档 |

⚠️ 两个仓库**独立**，不要共用 remote。

### 技术栈

- **输入法核心**：C++20，TSF (Text Services Framework)
- **构建系统**：CMake + Ninja
- **编译器**：VS 2026 BuildTools (VS18)，`vcvars64.bat` 初始化环境
  - ⚠️ `vswhere` 识别不了 VS18，不要用它找工具链
- **CRT 链接**：静态 `/MT`（Release）和 `/MTd`（Debug），**禁止分发 Debug CRT**

### 项目结构

```
OwO-v2/
├── apps/                   # 独立可执行程序
│   ├── core_service/       # Core 服务（词典、引擎、管道、语音）
│   ├── voice_input_plugin/ # 语音识别插件（SAPI，独立进程）
│   └── ipc_shell/          # IPC 命令行测试工具
├── src/
│   ├── engine/             # 拼音引擎（lexicon, candidate_generator, schema）
│   ├── tsf/                # TSF 输入法 DLL（text_service, candidate_window）
│   ├── ipc/                # 命名管道 IPC
│   ├── voice/              # OWVH 语音协议编解码
│   ├── config/             # 配置系统
│   ├── plugin/             # 插件系统（owopkg 沙箱）
│   └── model/              # 端侧模型接口
├── include/owo/            # 公共头文件
├── tests/                  # 测试
├── web/                    # 官网静态站（GitHub Pages）
├── packaging/              # 打包脚本
├── benchmarks/             # 性能基准
└── CMakeLists.txt
```

---

## 2. 新机器从零搭建

### 2.1 必备软件

1. **VS 2026 BuildTools**（或 VS 2026 Community）
   - `C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat`
   - 需要 C++ 桌面开发工作负载
2. **Git** + GitHub SSH 密钥
3. **CMake** >= 3.28
4. **Ninja**（CMake 生成器用 `-G Ninja`）

### 2.2 克隆 + 配置

```powershell
# 克隆 OwO
git clone git@github.com:3311930677/Intelligent-Input-Method-v2.git OwO-v2
cd OwO-v2

# 克隆灵犀（可选，独立项目）
cd ..
git clone git@github.com:3311930677/LingXi-DesktopAgent.git cross-app-assistant
```

### 2.3 CMake 配置

开发构建（punct-v3，日常调试用）：
```powershell
mkdir build\punct-v3
cd build\punct-v3
cmake ..\.. -G Ninja -DCMAKE_BUILD_TYPE=Debug
```

分发构建（Release，打包/评测用）：
```powershell
mkdir build\release-portable
cd build\release-portable
cmake ..\.. -G Ninja -DCMAKE_BUILD_TYPE=Release
```

### 2.4 构建

每次构建前先初始化 MSVC 环境：

```powershell
cmd /c "call `"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat`" && cd /d d:\UGit\OwO-v2\build\punct-v3 && ninja owo_tsf owo_core_service"
```

常用 targets：
- `owo_tsf` — TSF 输入法 DLL
- `owo_core_service` — Core 服务
- `owo_voice_input_plugin` — 语音插件
- `owo_ipc_shell` — IPC 调试工具
- `owo_engine_benchmark` — 性能基准工具
- `owo_voice_protocol_tests` — 语音协议测试

---

## 3. 开发部署（本机）

### 3.1 注册 TSF 输入法

dev 环境 CLSID：`{6d31c9b1-8978-4f49-89b4-66eb1e741591}`

```powershell
# 注册 dev 版本
regsvr32 d:\UGit\OwO-v2\build\punct-v3\OwO.TSF.dll
```

### 3.2 启动/重启 Core

Core 通过 Windows 计划任务 `OwO Core Service` 管理：

```powershell
Stop-ScheduledTask "OwO Core Service"
Start-ScheduledTask "OwO Core Service"
```

⚠️ TSF DLL 文件被占用时重启会失败（输入法正在使用），需先切到其他输入法。

### 3.3 验证 Core 正常

```powershell
# 管道连通性 + 候选正确性
d:\UGit\OwO-v2\build\punct-v3\owo_ipc_shell.exe nihao

# Core 加载完成时工作集 > 1GB（词典已全部映射）
Get-Process owo_core_service | Select WorkingSet64
```

---

## 4. 关键架构知识

### 4.1 TSF ↔ Core IPC

```
输入法 TSF DLL (OwO.TSF.dll)
  │  按键 / 选字
  ▼
命名管道 (named_pipe.cpp)
  │  protocol::Message 序列化
  ▼
Core 服务 (owo_core_service.exe)
  │  engine::CandidateGenerator
  ▼  候选列表 → 管道 → TSF → 自绘候选窗
```

### 4.2 语音输入链路（08-07 重构后）

```
TSF (F9 热键)
  │  voice_start/poll/cancel_request (命名管道 IPC)
  ▼
Core VoiceProcessController (--stdio 常驻 Broker)
  │  OWVH 二进制帧协议 (4字节LE长度前缀 + 载荷)
  │  hello→ACK→start_request+Grant→ACK→partial_result→final_result
  ▼
owo_voice_input_plugin.exe --stdio (独立进程)
  │  Windows SAPI InprocRecognizer + Dictation 语法
  ▼  SPEI_HYPOTHESIS/RECOGNITION → 文本
```

### 4.3 性能架构

- **每键全量 re-parse** + chart beam search，路径数 O(2^n)
- 长输入 (>8 字符) 用 cap=2 限流 incomplete path
- **根因**：商业输入法用增量 lattice + Viterbi (O(n))，OwO 是每次全量
- **中期方向**：重构为音节图 + Viterbi

---

## 5. 最重要的教训 (Don't Repeat)

### 5.1 Debug/Release 天壤之别

| | Debug | Release |
|--|-------|---------|
| Core 大小 | ~4.7 MB | ~856 KB |
| 延迟 | 500-2000ms | 13-96ms |
| 速度比 | 基准 | **10-21 倍快** |

- **性能结论必须用 Release 构建**，Debug 数字只做相对比较
- **判据用文件大小**，不要扫 CRT 字符串（静态 `/MTd` 不会导入 ucrtbased.dll）
- CRLF 链接：CMake 设的 `MultiThreaded$<$<CONFIG:Debug>:Debug>` — Debug 也是静态 CRT

### 5.2 测速纪律

```powershell
# 基准测试工具（进程内直测，1000样本）
owo_engine_benchmark.exe <lexicon.owolx> [--assisted]

# IPC 工具测端到端（每次含 ~15ms Release / ~38ms Debug 进程启动开销，须扣除）
owo_ipc_shell.exe nihao --benchmark

# 首次查询有一次性词典冷加载（page-in），约 1s，后续正常
```

### 5.3 C++ 数据结构陷阱

1. **`Candidate.source_segments` 覆盖整个输入**，不只 consumed 部分
   - 复合候选拼接时，必须按 `consumed_input_bytes` 截前导分段
   - 直接用 `prefix_cand.source_segments` 会导致预览重复

2. **从 `unordered_map` 收集后排序，tie-break 必须完全确定**
   - 仅按"文本长度降序"不可靠（同长候选哈希序随机丢失）
   - 加 `candidate.score` 降序 + `consumed/text` 做确定性 tie-break

3. **英文识别条件**：仅当输入无法 exact 解析为全多字母(≥2)完整音节才插英文首位
   - 必须排除 corrected path（hello→he+li+lou 是容错路径不算）
   - 否则 women/beijing/shanghai 被英文候选抢首位

### 5.4 测试时 Core 占用陷阱

⚠️ 本机可能同时存在**旧安装版 Core**（`C:\Users\<user>\AppData\Local\Programs\OwO\bin\`）和 **dev Core**。测试前用 WMI 确认实际运行路径：

```powershell
Get-WmiObject Win32_Process -Filter "name='owo_core_service.exe'" |
    Select CommandLine, ProcessId
```

---

## 6. 已知问题（待修）

| 问题 | 症状 | 方向 |
|------|------|------|
| 部分词频排序 | zhineng→只能(非智能)、jingdong→惊动 | 词典词频/语言模型排序 |
| 极端不完整拼音 | zzzzzzzzz 仍较慢(~100ms Release) | cap 限流已缓解，需要 Viterbi 重构 |
| 多n解析 | zhemezhinneng 只出"这么" | 长串辅音分词 |
| suoyidangwm | 需要复合候选拼接（已实现但受限） | 词典收入或提升拼接上限 |
| 中间叹词合并 | 撤销中（会强制改输入内容） | 只改候选不改显示 |

---

## 7. 分发与发布

### 7.1 发布链路

```
GitHub Pages (web/)     ← 官网静态站，push main 自动部署
GitHub Releases         ← 大文件（Setup.exe / ZIP / plugins）
```

### 7.2 打包

```powershell
# 便携版 ZIP
.\packaging\portable\make-package.ps1

# 安装版 Setup.exe
.\packaging\build-installer.ps1
```

两个脚本都从 `build\release-portable\` 取 Release 产物，有 PE 预检拒绝 Debug CRT。

### 7.3 发版步骤

1. 更新 `web/config.js` 中 `DOWNLOAD_BASE_URL` 和版本号/sha256
2. `git commit && git push` main
3. 在 GitHub 建 tag（如 `v0.9.2`）→ Release → 上传 `web/releases/` 下的 Setup.exe + ZIP
4. GitHub Pages 自动从 `main/web` 部署

### 7.4 当前状态

- **v0.9.1 已打包**（`web/releases/`，sha256 已写入 config.js）
- **尚未上传 GitHub Release**（需人工手动：建 tag v0.9.1 → 传两个资产）
- `web/releases/` 和 `web/plugins/` 在 `.gitignore` 排除，不进 git
- 官网校验：`packaging\verify-site.ps1`

---

## 8. 灵犀 (LingXi) 要点

- 独立仓 `d:\UGit\cross-app-assistant`，不与 OwO 共用 remote
- Rust / Tauri v2 桌面应用
- 通过 `RegisterHotKey` 响应全局快捷键（无全局钩子）
- 构建 `apps/overlay` 需 WebView2 Runtime
- 云端模型兼容 OpenAI API key
- **当前是独立原型**，L3 接入 OwO 是结项目标（尚未完成）
- UIA 文本读写、选区辅助、Diff/撤销等能力在规划中

---

## 9. 大创项目要点

- 文档目录：`d:\UGit\大创\`，**不在 git**
- `市级.pdf`（2026-06-09 申报书）不可修改
- 其余四份文档（总体实施方案/技术实现路线/UIA方案/现状评估）已于 08-06 重写对齐实际
- **申报书 vs 现实差距**：VSCode 代码辅助 / 鼠标执行 / 主动模式 / GAT 均未实现
- 结项要求：软著 1 + 发明 1（可收敛为选区上下文+级联推理）+ 评测集 200 条 + 安装包
- **结项策略（08-06 定稿）**：优先保 A（输入体验+软著+数据集），B/C 延期可接受
- **跨平台表述**：申报书有安卓/iOS 内容，实际为 Windows 项目，答辩时需对齐

---

## 10. 日常命令速查

```powershell
# === 构建 ===
# 初始化环境
cmd /c "call `"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat`" && cd /d d:\UGit\OwO-v2\build\punct-v3 && cmd"

# 增量构建
ninja owo_tsf owo_core_service owo_voice_input_plugin

# === 部署 ===
regsvr32 d:\UGit\OwO-v2\build\punct-v3\OwO.TSF.dll   # 注册 TSF
Stop-ScheduledTask "OwO Core Service"                    # 停 Core
Start-ScheduledTask "OwO Core Service"                   # 启 Core

# === 调试 ===
owo_ipc_shell.exe nihao                                 # IPC 快速测试
Get-Process owo_core_service | Select Id, WorkingSet64   # Core 状态
Get-Process owo_voice_input_plugin -ErrorAction SilentlyContinue  # 语音进程

# === 性能 ===
owo_engine_benchmark.exe lexicon.owolx --assisted        # 进程内基准（Release 才准）
```
