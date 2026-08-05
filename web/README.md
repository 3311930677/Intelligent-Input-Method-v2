# OwO 输入法官网 (web/)

纯静态官网：**下载输入法本体** + **浏览/下载插件库**。零后端，可直接托管到 GitHub Pages、简幻欢免费服务器、对象存储或任意静态空间。

## 目录结构

```
web/
├── index.html          # 首页（下载本体 + 插件库）
├── styles.css # 样式（深色玻璃拟态、响应式）
├── app.js   # 逻辑：加载 catalog、渲染插件、搜索/筛选、拼下载链接
├── config.js           # ★ 唯一需要改的配置：下载源、版本、链接
├── catalog.json        # 插件清单（与设置中心内的插件库共用同一格式）
└── assets/             # logo 与插件图标 (SVG)
```

## 本地预览

网页用 `fetch` 读 `catalog.json`，必须经 HTTP（不能直接双击打开 `file://`）。任选一种起本地服务器：

```powershell
# Node
npx --yes serve web

# 或 Python
cd web; python -m http.server 8080
```

然后浏览器打开 `http://localhost:8080`。

## 换下载源（关键）

只改 `web/config.js` 一个文件：

- `DOWNLOAD_BASE_URL`：安装包与 `.owopkg` 所在的根地址。
  - 同站托管：保持 `"./"`
  - GitHub Releases：填 `https://github.com/<user>/<repo>/releases/download/<tag>/`
  - 简幻欢服务器：填 `https://你的域名或IP/owo/`（把文件放到该目录即可）
- `INSTALLER.url`：安装包文件名或完整 URL。
- `CATALOG_URL`：插件清单地址（默认同站 `./catalog.json`）。

`catalog.json` 里每个插件的 `package` 字段可以是**相对路径**（相对 `DOWNLOAD_BASE_URL`）或**完整 https URL**。

## catalog.json 字段

| 字段 | 说明 |
|------|------|
| `id` | 插件唯一 ID（如 `com.owo.emoji`）|
| `name` / `summary` | 显示名 / 简介 |
| `version` / `author` | 版本 / 作者 |
| `category` | 分类（用于筛选标签）|
| `trust` | 信任级：`official` / `verified` / `community` |
| `icon` | 图标路径（相对本站 assets）|
| `network` | 是否需要联网（用于展示"纯本地/需要联网"标签）|
| `package` | `.owopkg` 下载路径（相对 `DOWNLOAD_BASE_URL` 或完整 URL）|
| `sizeLabel` / `sha256` | 大小展示 / 校验值 |

## 部署到简幻欢免费服务器（示例）

服务器只需能提供静态文件（HTTP）即可，最低配足够：

```bash
# 在服务器上，用 nginx 或临时用 python 提供 web/ 目录
cd /var/www/owo && python3 -m http.server 80
# 或把 web/ 拷进 nginx 的站点根目录
```

把 `.owopkg` 与安装包一并放到 `DOWNLOAD_BASE_URL` 指向的目录，网页的"下载"按钮即可直接取包。

## 部署到 GitHub Pages（免服务器）

1. 把 `web/` 内容推到 `gh-pages` 分支或仓库 `docs/` 目录并在仓库设置里开启 Pages。
2. 把安装包与 `.owopkg` 作为 Release 资产上传，`config.js` 的 `DOWNLOAD_BASE_URL` 指向 Release 下载地址。
