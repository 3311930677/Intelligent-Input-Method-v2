// OwO Input Method — website runtime configuration.
//
// This single file is the only place that needs editing when the download
// origin changes (GitHub Releases, a self-hosted server such as the free
// 简幻欢 instance, an object-storage bucket, or a CDN). The website and the
// in-app plugin library are designed to read the SAME catalog.json, so the
// distribution story stays consistent across the desktop app and the web.
//
// How to point at a different origin:
//   1. Set DOWNLOAD_BASE_URL to wherever the release artifacts live.
//   2. Make sure catalog.json and the .owopkg / installer files are reachable
//      under that base URL (or override the absolute URLs per entry below).
window.OWO_SITE_CONFIG = {
  // Base URL that hosts the installer and plugin packages. Trailing slash
  // optional. Leave as "./" to serve everything from the same static site.
  DOWNLOAD_BASE_URL: "./",

  // Relative (to DOWNLOAD_BASE_URL) or absolute URL of the plugin catalog.
  CATALOG_URL: "./catalog.json",

// Desktop installer metadata. If INSTALLER.url is absolute it is used as-is;
// otherwise it is resolved against DOWNLOAD_BASE_URL.
  INSTALLER: {
    version: "0.9.0",
    // 图形化安装包（Inno Setup）：双击运行，免管理员，装完自动注册并启动服务。
    // 免安装便携版 ZIP 见首页下载区的备选链接。
    url: "releases/OwO-InputMethod-Setup-0.9.0-win-x64.exe",
    sizeLabel: "17.5 MB · 安装版（静态运行库，无需 VC++ redist）",
    minOs: "Windows 10 1809 / Windows 11 (x64)",
    // sha256 of the installer, for manual verification:
    // 575cab5864fd9104eb13506f46342e8dd93b9224a3b2103eca4ee365512764f7
  },

  // Project links.
  LINKS: {
    github: "https://github.com/3311930677/Intelligent-Input-Method-v2",
    issues: "https://github.com/3311930677/Intelligent-Input-Method-v2/issues",
    license: "https://www.gnu.org/licenses/gpl-3.0.html",
  },
};
