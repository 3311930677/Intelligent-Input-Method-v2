// OwO Input Method website logic.
// Loads the shared plugin catalog, renders installer + plugin library, and
// resolves every download URL against the configurable DOWNLOAD_BASE_URL so
// the same static site works on GitHub Pages, a self-hosted server, or a CDN.
(function () {
  "use strict";

  var cfg = window.OWO_SITE_CONFIG || {};

  // --- URL helpers ---------------------------------------------------------
  function isAbsolute(url) {
    return /^https?:\/\//i.test(url) || url.indexOf("//") === 0;
  }
  function joinUrl(base, path) {
    if (!path) return base;
    if (isAbsolute(path)) return path;
    var b = (base || "./").replace(/\/+$/, "");
    var p = String(path).replace(/^\/+/, "");
    return b + "/" + p;
  }
  function resolveDownload(path) {
    return joinUrl(cfg.DOWNLOAD_BASE_URL || "./", path);
}

  // --- Static links --------------------------------------------------------
  function applyLinks() {
    var links = cfg.LINKS || {};
    setHref("navGithub", links.github);
    setHref("footGithub", links.github);
    setHref("footIssues", links.issues);
    setHref("footLicense", links.license);
  setHref("dlSource", links.github);
  }
  function setHref(id, url) {
    var el = document.getElementById(id);
    if (el && url) el.setAttribute("href", url);
  }
  function setText(id, text) {
    var el = document.getElementById(id);
    if (el) el.textContent = text;
  }

  // --- Installer -----------------------------------------------------------
  function applyInstaller() {
    var ins = cfg.INSTALLER || {};
    var versionLabel = ins.version ? "v" + ins.version : "";
    setText("heroVersion", versionLabel);
    setText("dlVersion", versionLabel);
    setText("dlSize", ins.sizeLabel || "");
    setText("dlOs", ins.minOs ? "系统要求：" + ins.minOs : "");
    setText("heroOs", ins.minOs ? "支持 " + ins.minOs : "");

    var url = ins.url ? resolveDownload(ins.url) : "#";
    var dlButton = document.getElementById("dlButton");
    if (dlButton) dlButton.setAttribute("href", url);
    var heroDownload = document.getElementById("heroDownload");
    if (heroDownload && ins.url) heroDownload.setAttribute("href", url);
  }

  // --- Plugin library ------------------------------------------------------
  var allPlugins = [];
  var activeCategory = "全部";
  var searchTerm = "";

  function trustLabel(trust) {
switch (trust) {
      case "official": return "官方";
      case "verified": return "已认证";
      case "community": return "社区";
   default: return trust || "未知";
    }
  }

  function pluginCard(p) {
    var card = document.createElement("article");
    card.className = "pcard";

    var head = document.createElement("div");
    head.className = "pcard__head";
    var icon = document.createElement("img");
    icon.className = "pcard__icon";
    icon.alt = p.name || p.id;
    icon.src = p.icon ? joinUrl(".", p.icon) : "assets/plugin-generic.svg";
    icon.onerror = function () { this.onerror = null; this.src = "assets/plugin-generic.svg"; };
    var titleWrap = document.createElement("div");
    var name = document.createElement("h3");
    name.className = "pcard__name";
    name.textContent = p.name || p.id;
 var ver = document.createElement("span");
    ver.className = "pcard__ver";
 ver.textContent = (p.version ? "v" + p.version : "") + (p.author ? " · " + p.author : "");
    titleWrap.appendChild(name);
    titleWrap.appendChild(ver);
    head.appendChild(icon);
    head.appendChild(titleWrap);

    var summary = document.createElement("p");
    summary.className = "pcard__summary";
    summary.textContent = p.summary || "";

    var meta = document.createElement("div");
    meta.className = "pcard__meta";
    meta.appendChild(makeTag(trustLabel(p.trust), "tag--official"));
  if (p.category) meta.appendChild(makeTag(p.category, ""));
    meta.appendChild(makeTag(p.network ? "需要联网" : "纯本地", p.network ? "tag--net" : ""));

    var actions = document.createElement("div");
    actions.className = "pcard__actions";
    var size = document.createElement("span");
    size.className = "pcard__size";
    size.textContent = p.sizeLabel || "";
    var dl = document.createElement("a");
    dl.className = "btn btn--primary";
    dl.textContent = "下载";
    if (p.package) {
      dl.setAttribute("href", resolveDownload(p.package));
      dl.setAttribute("download", "");
    } else {
      dl.setAttribute("href", "#");
      dl.setAttribute("aria-disabled", "true");
    dl.style.opacity = "0.5";
      dl.style.pointerEvents = "none";
    }
    actions.appendChild(size);
    actions.appendChild(dl);

    card.appendChild(head);
    card.appendChild(summary);
    card.appendChild(meta);
    card.appendChild(actions);
    return card;
  }

  function makeTag(text, extra) {
    var t = document.createElement("span");
    t.className = "tag" + (extra ? " " + extra : "");
    t.textContent = text;
    return t;
  }

  function renderFilters() {
    var wrap = document.getElementById("pluginFilters");
    if (!wrap) return;
    var cats = ["全部"];
    allPlugins.forEach(function (p) {
      if (p.category && cats.indexOf(p.category) === -1) cats.push(p.category);
    });
    wrap.innerHTML = "";
    cats.forEach(function (c) {
      var chip = document.createElement("button");
      chip.className = "chip" + (c === activeCategory ? " on" : "");
      chip.textContent = c;
      chip.addEventListener("click", function () {
        activeCategory = c;
     renderFilters();
        renderGrid();
      });
    wrap.appendChild(chip);
    });
  }

  function renderGrid() {
    var grid = document.getElementById("pluginGrid");
    var empty = document.getElementById("pluginEmpty");
    if (!grid) return;
    var term = searchTerm.trim().toLowerCase();
    var list = allPlugins.filter(function (p) {
  var catOk = activeCategory === "全部" || p.category === activeCategory;
      var hay = ((p.name || "") + " " + (p.summary || "") + " " + (p.id || "")).toLowerCase();
      var termOk = !term || hay.indexOf(term) !== -1;
      return catOk && termOk;
    });
    grid.innerHTML = "";
    list.forEach(function (p) { grid.appendChild(pluginCard(p)); });
    if (empty) empty.hidden = list.length !== 0;
  }

  function loadCatalog() {
    var grid = document.getElementById("pluginGrid");
    var url = cfg.CATALOG_URL ? joinUrl(".", cfg.CATALOG_URL) : "catalog.json";
    fetch(url, { cache: "no-cache" })
      .then(function (r) {
    if (!r.ok) throw new Error("HTTP " + r.status);
   return r.json();
      })
  .then(function (data) {
        allPlugins = (data && data.plugins) || [];
        renderFilters();
        renderGrid();
      })
      .catch(function (err) {
     if (grid) {
   grid.innerHTML =
     '<p style="color:var(--muted);grid-column:1/-1;text-align:center">' +
            "插件库加载失败（" + err.message + "）。请确认 catalog.json 可访问。</p>";
    }
      });
  }

  function bindSearch() {
    var input = document.getElementById("pluginSearch");
    if (!input) return;
    input.addEventListener("input", function () {
      searchTerm = input.value;
      renderGrid();
    });
  }

  // --- Init ----------------------------------------------------------------
  document.addEventListener("DOMContentLoaded", function () {
    applyLinks();
    applyInstaller();
    bindSearch();
    loadCatalog();
  });
})();
