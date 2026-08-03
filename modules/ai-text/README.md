# OwO AI 文本处理模块（占位）

- 分级：L2（本地计算 + 受控网络白名单）
- 形态：**独立可下载模块**，非 owopkg 沙箱插件。用户按需下载安装。
- 状态：占位，未实现。

## 定位

在 OwO 之外作为独立进程运行，向 OwO 申请：

- 网络能力（`network.domain`，仅白名单域名，如所选 LLM 供应商 API）；
- 动作能力（`selection.replace` / `text.commit`，用于把处理结果写回）。

经 `docs/architecture/l3-capability-protocol.md` 定义的能力授权协议接入。

## 计划能力

- 润色：保留原意改善表达。
- 纠错：只修正错别字、标点、语法。
- 提示词增强：补全角色/目标/约束/输出格式。
- 模型后端：本地小模型或 OpenAI-compatible 云端（API Key 不落盘）。

## 为什么不做成 owopkg

owopkg 沙箱禁网络（ADR 0012），AI 模块需要访问云端 API，因此走 L3 独立模块 + 网络白名单授权，而非塞进零能力沙箱。详见 `docs/architecture/plugin-platform-design.md`。

## 下一步

实现前需冻结：网络白名单 manifest 字段、能力授权协议 schema、对应 ADR。
