# ADR-0031：开发者本地插件测试签名流程

- 状态：接受
- 日期：2026-08-03

## 背景

P3C 冻结了插件安装的强信任策略（ADR 0019/0021）：`.owopkg` 必须带分离式 CMS SHA-256 签名，签名证书需含代码签名 EKU，并通过 Windows Authenticode 强签名链在本机受信任存储中完全验证；P3C 明确不提供未签名开发者模式。P3C 验收报告也指出，项目当前没有发行代码签名证书，真实签名闭环留到 P5。

这带来一个开发期困境：在没有发行证书之前，开发者无法把新插件（如 P4 表情包插件）真正安装进版本化存储并由 PluginHost 拉起来做端到端测试。

## 决策

引入一套**仅限本地开发**的测试签名流程，**不修改任何签名验证代码**（`package_signature.cpp` 一行未动）。核心事实：`verify_package_signature_trust` 没有硬编码任何特定发行证书，只要求签名证书链能在本机受信根存储中验通。因此开发者信任通过“本机临时信任一个自签名测试根”实现，而非在代码中开后门。

组件：

- `tools/plugin_pack/main.cpp`（`owo_plugin_pack.exe`）：用 C++ 复用与 `inspect_package` **逐字节一致**的清单摘要算法，仅生成 Store 方式的 `.owopkg`，避免依赖受控 zlib Deflate 路径。提供 `--inventory`（输出待签名的 canonical inventory SHA-256）与 `--pack`（组装最终包，可注入根 `signature.json`）两种模式。
- `scripts/build-dev-plugin.ps1`：创建自签名代码签名测试证书（`CN=OwO Plugin Developer Test`，仅存 CurrentUser\My），临时加入 CurrentUser\Root，算 inventory，用 .NET `SignedCms` 生成分离 CMS SHA-256 签名，写出 `signature.json`，调 `owo_plugin_pack` 产出最终包。
- `scripts/remove-dev-plugin-cert.ps1`：从 CurrentUser 的 Root 与 My 存储移除该测试证书，撤销开发信任。

约束：

- 测试证书只存在于当前用户的证书存储，随用随删；绝不用于发行签名。
- 该流程只影响“本机是否信任开发者测试根”，不改变生产安装事务、AppContainer 沙箱、权限模型或任何验证逻辑。
- 打包工具只产出 Store 条目、无 ZIP64、无 data descriptor、本地头与中央目录一致、非 ASCII 路径置 UTF-8 flag，确保被现有严格预检接受。

## 备选方案

1. **在代码里加“开发者模式”跳过签名校验**：会永久性削弱安全边界、复用可信状态，被 ADR 0019 明确禁止。拒绝。
2. **用 PowerShell 手写清单摘要算法**：摘要格式复杂（域分隔符 + 每条目路径/CRC/大小/压缩数据 SHA-256 的定长二进制拼接），手写极易与 C++ 端产生字节级不一致。改用 C++ 工具复用同一算法，保证一致。
3. **等 P5 发行证书**：阻塞 P4 全部端到端测试。不可接受。

## 影响

- P4 及以后可在本机完成“签名验证成功 → 设置中心/CLI 安装并启用 → PluginHost 拉起 → 调用”的完整闭环测试。
- 安全叙事不受损：验证代码零改动，信任仅来自开发者显式、可撤销的本机证书。
- 新增一个 Windows-only 构建目标 `owo_plugin_pack` 与两个脚本，不进入 TSF/Core 运行链路。

## 回滚或迁移

- 运行 `scripts/remove-dev-plugin-cert.ps1` 即可撤销全部开发信任。
- 从 CMake 移除 `owo_plugin_pack` 目标、删除两个脚本即可完全回退；不影响任何既有插件安装、存储或验证路径。
- P5 引入真实发行证书与时间戳策略后，本流程仅用于开发，不参与发布。
