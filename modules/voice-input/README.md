# OwO 语音识别输入模块

- 分级：L3（硬件能力：麦克风）
- 形态：**独立可下载模块**，非 owopkg 沙箱插件。用户按需下载安装。
- 状态：首个可运行切片已实现（Windows SAPI 本地听写 + OWVH 协议）。

## 实现位置

- 协议编解码：`include/owo/voice/voice_protocol.h`、`src/voice/voice_protocol.cpp`（库 `owo_voice_contract`）。
- 独立进程插件：`apps/voice_input_plugin/main.cpp`（可执行 `owo_voice_input_plugin`）。
- Manifest：`apps/voice_input_plugin/l3-manifest.json`。
- 协议测试：`tests/voice/voice_protocol_tests.cpp`（CTest `owo.voice.protocol`）。

## 运行形态

`owo_voice_input_plugin` 作为独立普通用户进程运行，两种模式：

- `--once`：单次识别，结果打印到 stdout（本地自测用）。
- `--stdio`：以 OWVH 二进制协议（长度前缀帧）与 OwO 能力代理经 stdio 通信。

握手后通告 `microphone.capture` 与 `voice.transcribe.v1`；`start_request` 必须携带匹配的 R3 `CapabilityGrant`（subject=插件 ID、scope=active-composition、能力含 microphone.capture、五分钟内有效、次数受限），否则拒绝启动识别。识别结果经 `partial_result`/`final_result` 返回，最终由 OwO 侧通过统一动作 `text.commit` 上屏。

## 能力与授权

- 权限：`microphone.capture`（R3，采集麦克风，每次/会话内明确确认）。
- 提供服务：`voice.transcribe.v1`。
- 请求动作：`text.commit.v1`（把识别结果上屏，统一能力接口）。
- 授权模型与协议见 `docs/architecture/l3-capability-protocol.md`。

## 依赖

- Windows SAPI（`sapi.h`，链接 `sapi`/`ole32`）做本地听写，需系统已安装对应语言识别引擎与默认麦克风。
- 识别引擎缺失、无麦克风、语言不支持时失败封闭并返回具体 HRESULT 诊断，不静默成功。

## 后续

- 冻结设备能力 manifest 字段与麦克风授权确认 UI（对应新 ADR）。
- 可插拔识别后端（本地 / 授权在线服务，在线需额外网络白名单授权）。
