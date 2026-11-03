// 生成的，别手改：packages/page-runtime/render.js 的拷贝。改那份，再跑 pnpm miniapp:sync。
/*
 * The page runtime (docs/design/feed.md, 展示页): draws a `yay.page/v1` spec as HTML. One renderer for every
 * client: the iOS app loads page.html in a WKWebView and calls YayPage.show(spec, options); the web will load
 * the same files in a sandboxed iframe. The spec is data, never code: every string is escaped, links must be
 * https and open outside the page. Every block only shows something; none of them acts.
 */
(function (root) {
  "use strict";

  function esc(value) {
    return String(value == null ? "" : value).replace(/[&<>"']/g, function (c) {
      return { "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c];
    });
  }
  function safeUrl(url) {
    return /^https:\/\/[^\s"'<>]+$/.test(String(url || "")) ? String(url) : null;
  }

  /** A small Markdown subset: paragraphs, **bold**, "- " lists and [links](https://…). */
  function inline(text) {
    var out = esc(text).replace(/\*\*([^*]+)\*\*/g, "<b>$1</b>");
    return out.replace(/\[([^\]]+)\]\((https:\/\/[^)\s]+)\)/g, function (_m, label, href) {
      return '<a href="' + href + '" data-link>' + label + "</a>";
    });
  }
  function markdown(text) {
    var html = "",
      list = [];
    function flush() {
      if (list.length) html += "<ul>" + list.map(function (i) { return "<li>" + inline(i) + "</li>"; }).join("") + "</ul>";
      list = [];
    }
    String(text)
      .split(/\n{2,}/)
      .forEach(function (para) {
        var lines = para.split("\n");
        if (lines.every(function (l) { return /^\s*[-•]\s+/.test(l); })) {
          lines.forEach(function (l) { list.push(l.replace(/^\s*[-•]\s+/, "")); });
          return;
        }
        flush();
        html += "<p>" + lines.map(inline).join("<br>") + "</p>";
      });
    flush();
    return html;
  }

  /** Words of `better` that `said` did not have, to mark what changed. */
  function marked(said, better) {
    var before = {};
    String(said)
      .toLowerCase()
      .split(/[^\p{L}\p{N}']+/u)
      .forEach(function (w) { if (w) before[w] = true; });
    return String(better)
      .split(/(\s+)/)
      .map(function (part) {
        var key = part.toLowerCase().replace(/[^\p{L}\p{N}']+/gu, "");
        return key && !before[key] ? "<mark>" + esc(part) + "</mark>" : esc(part);
      })
      .join("");
  }

  function chart(block) {
    var points = block.points,
      w = 340,
      h = 140,
      base = 112,
      top = 22,
      max = Math.max.apply(null, points.map(function (p) { return p.value; }).concat([1])),
      step = (w - 20) / points.length,
      stacked = Array.isArray(block.series) && block.series.length > 1,
      svg = '<svg viewBox="0 0 ' + w + " " + h + '" role="img" aria-label="' + esc(block.title || "图表") + '">';
    svg += '<line class="axis" x1="10" x2="' + (w - 10) + '" y1="' + base + '" y2="' + base + '"/>';
    var path = "";
    points.forEach(function (p, i) {
      var cx = 10 + step * i + step / 2,
        y = base - (p.value / max) * (base - top),
        last = i === points.length - 1;
      if (block.kind === "bar" && stacked) {
        var sw = Math.min(26, step * 0.6),
          floor = base;
        (p.parts || []).forEach(function (part, k) {
          if (!(part > 0) || !block.series[k]) return;
          var ph = (part / max) * (base - top);
          svg += '<rect class="t-' + toneOf(block.series[k].tone, k) + '" x="' + (cx - sw / 2).toFixed(1) + '" y="' + (floor - ph).toFixed(1) + '" width="' + sw.toFixed(1) + '" height="' + ph.toFixed(1) + '"/>';
          floor -= ph;
        });
      } else if (block.kind === "bar") {
        var bw = Math.min(26, step * 0.6);
        if (p.value > 0)
          svg += '<rect class="' + (last ? "hot" : "bar") + '" x="' + (cx - bw / 2).toFixed(1) + '" y="' + y.toFixed(1) + '" width="' + bw.toFixed(1) + '" height="' + (base - y).toFixed(1) + '" rx="5"/>';
      } else {
        path += (i ? "L" : "M") + cx.toFixed(1) + " " + y.toFixed(1);
        if (last) svg += '<circle class="hot" cx="' + cx.toFixed(1) + '" cy="' + y.toFixed(1) + '" r="4"/>';
      }
      svg += '<text x="' + cx.toFixed(1) + '" y="' + (base + 18) + '" text-anchor="middle">' + esc(p.label) + "</text>";
      if (block.kind === "bar" || last)
        svg += '<text class="' + (last ? "val strong" : "val") + '" x="' + cx.toFixed(1) + '" y="' + (y - 6).toFixed(1) + '" text-anchor="middle">' + (p.value || "—") + "</text>";
    });
    if (path) svg += '<path class="line" d="' + path + '"/>';
    return '<figure class="chart">' + (block.title ? "<figcaption>" + esc(block.title) + "</figcaption>" : "") + svg + "</svg>" + (stacked ? legend(block.series) : "") + "</figure>";
  }

  var TONES = ["coral", "sage", "sun", "ink", "wake", "focus", "sleep", "mine"];
  /** A tone the page knows, or one by position. */
  function toneOf(tone, index) {
    return TONES.indexOf(tone) >= 0 ? tone : ["sage", "coral", "sun", "ink"][index % 4];
  }
  function legend(items) {
    return '<div class="legend">' + items.map(function (s, k) { return '<span><i class="t-' + toneOf(s.tone, k) + '"></i>' + esc(s.label) + "</span>"; }).join("") + "</div>";
  }

  /** 日历热力: weeks as columns, Monday on top, deeper for more; days after the last value stay empty. */
  function calendar(block) {
    var values = block.values,
      parts = String(block.start).split("-").map(Number),
      first = new Date(Date.UTC(parts[0], parts[1] - 1, parts[2])),
      lead = (first.getUTCDay() + 6) % 7,
      max = Math.max.apply(null, values.concat([1])),
      cells = lead + values.length,
      weeks = Math.ceil(cells / 7),
      size = 12,
      gap = 3,
      w = weeks * (size + gap),
      h = 7 * (size + gap),
      tone = ["coral", "sage", "sun", "ink"].indexOf(block.tone) >= 0 ? block.tone : "sage",
      svg = '<svg class="cal cal-' + tone + '" viewBox="0 0 ' + w + " " + h + '" role="img" aria-label="' + esc(block.title || "日历") + '">';
    values.forEach(function (v, i) {
      var at = lead + i,
        x = Math.floor(at / 7) * (size + gap),
        y = (at % 7) * (size + gap),
        level = v > 0 ? Math.min(4, 1 + Math.floor((v / max) * 3.999)) : 0;
      svg += '<rect class="l' + level + '" x="' + x + '" y="' + y + '" width="' + size + '" height="' + size + '" rx="3"><title>' + esc(v) + "</title></rect>";
    });
    return '<figure class="chart calendar">' + (block.title ? "<figcaption>" + esc(block.title) + "</figcaption>" : "") + svg + "</svg>" +
      '<div class="legend scale cal-' + tone + '"><span>少</span><i class="l1"></i><i class="l2"></i><i class="l3"></i><i class="l4"></i><span>多' + (block.unit ? " · " + esc(block.unit) : "") + "</span></div></figure>";
  }

  /** 环: each item's share of the whole, its label and number beside it. */
  function ring(block) {
    var items = block.items,
      total = items.reduce(function (n, i) { return n + (i.value > 0 ? i.value : 0); }, 0),
      r = 42,
      c = 2 * Math.PI * r,
      offset = 0,
      svg = '<svg viewBox="0 0 110 110" role="img" aria-label="' + esc(block.title || "占比") + '"><circle class="track" cx="55" cy="55" r="' + r + '"/>';
    items.forEach(function (item, k) {
      if (!(item.value > 0) || !total) return;
      var len = (item.value / total) * c;
      svg += '<circle class="arc t-' + toneOf(item.tone, k) + '" cx="55" cy="55" r="' + r + '" stroke-dasharray="' + len.toFixed(2) + " " + (c - len).toFixed(2) + '" stroke-dashoffset="' + (-offset).toFixed(2) + '" transform="rotate(-90 55 55)"/>';
      offset += len;
    });
    if (block.center) svg += '<text class="big" x="55" y="56" text-anchor="middle">' + esc(block.center.value) + '</text><text x="55" y="72" text-anchor="middle">' + esc(block.center.label) + "</text>";
    svg += "</svg>";
    var rows = items.map(function (item, k) {
      var share = total ? Math.round((item.value / total) * 100) : 0;
      return '<div><i class="t-' + toneOf(item.tone, k) + '"></i><span>' + esc(item.label) + "</span><b>" + share + "%</b></div>";
    }).join("");
    return '<figure class="chart ring">' + (block.title ? "<figcaption>" + esc(block.title) + "</figcaption>" : "") + '<div class="ring-body">' + svg + '<div class="ring-rows">' + rows + "</div></div></figure>";
  }

  /*
   * Service icons: the ones the round screen shows when Yay opens each app (its 铺满帧), drawn as in the iOS app's
   * ServiceIcon, in the round screen's colours in light and dark alike (extension-platform.md §8.6: one icon on the
   * device, on a post's author and on the service's page). A 100-unit box, the disc filling it.
   */
  var ICONS = {
    "language-buddy":
      '<circle cx="50" cy="50" r="50" fill="#DF795D"/>' +
      '<rect x="31.28" y="35.44" width="29.12" height="21.84" rx="7.8" fill="#9FB38C"/><polygon points="38.56,57.28 35.44,63.52 43.76,57.28" fill="#9FB38C"/>' +
      '<rect x="40.64" y="41.68" width="29.12" height="21.84" rx="7.8" fill="#FAF7EF"/><polygon points="62.48,63.52 66.64,69.76 58.32,63.52" fill="#FAF7EF"/>' +
      '<text x="55.2" y="53.12" font-size="13.5" font-weight="500" fill="#DF795D" text-anchor="middle" dominant-baseline="central">Aa</text>',
    "homework-buddy":
      '<circle cx="50" cy="50" r="50" fill="#DF795D"/>' +
      '<path d="M50 28.54V71.46M28.54 50H71.46" stroke="#FAF7EF" stroke-opacity=".55" stroke-width="1.07" stroke-dasharray="3 2.58"/>' +
      '<rect x="28.54" y="28.54" width="42.92" height="42.92" rx="2.15" fill="none" stroke="#FAF7EF" stroke-width="1.72"/>' +
      '<text x="50" y="50" font-size="34.3" fill="#171C19" text-anchor="middle" dominant-baseline="central">字</text>',
    breathe:
      '<circle cx="50" cy="50" r="50" fill="#171C19"/><circle cx="50" cy="50" r="45.92" fill="none" stroke="#9FB38C" stroke-width="2.25"/>' +
      '<circle cx="50" cy="50" r="25.75" fill="#F3E6C8"/>',
    "party-play":
      '<circle cx="50" cy="50" r="50" fill="#DF795D"/>' +
      '<rect x="-3" y="-1.5" width="6" height="3" fill="#9FB38C" transform="translate(22.5 29) rotate(-94.5)"/>' +
      '<rect x="-3" y="-1.5" width="6" height="3" fill="#E3A857" transform="translate(75 25) rotate(85.9)"/>' +
      '<rect x="-3" y="-1.5" width="6" height="3" fill="#F3E6C8" transform="translate(79 69) rotate(99.7)"/>' +
      '<rect x="-3" y="-1.5" width="6" height="3" fill="#E3A857" transform="translate(25 75) rotate(-85.9)"/>' +
      '<rect x="-3" y="-1.5" width="6" height="3" fill="#F3E6C8" transform="translate(52.5 16) rotate(8.6)"/>' +
      '<g transform="translate(50 52) rotate(-11.46)"><rect x="-19.85" y="-19.45" width="41" height="41" rx="8.2" fill="#000" fill-opacity=".35"/>' +
      '<rect x="-20.5" y="-20.5" width="41" height="41" rx="8.2" fill="#F6EFE0"/>' +
      '<g fill="#2A2F2B"><circle cx="-11.07" cy="-11.07" r="3.49"/><circle cx="11.07" cy="-11.07" r="3.49"/><circle r="3.49"/><circle cx="-11.07" cy="11.07" r="3.49"/><circle cx="11.07" cy="11.07" r="3.49"/></g></g>',
  };
  function serviceIcon(service) {
    var drawn = Object.prototype.hasOwnProperty.call(ICONS, service.slug) ? ICONS[service.slug] : null;
    return drawn
      ? '<span class="svc-icon drawn"><svg viewBox="0 0 100 100" aria-hidden="true">' + drawn + "</svg></span>"
      : '<span class="svc-icon">' + esc(service.icon) + "</span>";
  }

  /** The service's head: its icon, name, who or which device, and when it was last used. */
  function serviceHead(service) {
    var line = [service.who, service.last].filter(Boolean).map(esc).join(" · ");
    return '<div class="svc">' + serviceIcon(service) + '<span class="svc-name"><b>' + esc(service.name) + "</b>" + (line ? "<small>" + line + "</small>" : "") + "</span></div>";
  }
  /**
   * Under a service's head the title and subtitle say only what the head does not: the service page's title is the
   * service's name and its subtitle the device and the last time, so both go; a day's or week's page keeps its title
   * and its subtitle without the service's name in front. (An older app shows no head, so the spec keeps them all.)
   */
  function headed(spec, service) {
    if (spec.title === service.name) return "";
    var subtitle = String(spec.subtitle || "");
    var prefix = service.name + " · ";
    if (subtitle.indexOf(prefix) === 0) subtitle = subtitle.slice(prefix.length);
    return "<h1>" + esc(spec.title) + "</h1>" + (subtitle ? "<p>" + esc(subtitle) + "</p>" : "");
  }
  function openAttr(ref) {
    return /^(day|week):\d{4}-\d{2}-\d{2}$/.test(String(ref || "")) ? ' data-open="' + esc(ref) + '"' : "";
  }

  var LANGS = { en: "en", zh: "zh-Hans", ja: "ja", es: "es" };
  /** The item's language, so the system picks the right face (Japanese kanji in a Japanese one). */
  function langAttr(lang) {
    return Object.prototype.hasOwnProperty.call(LANGS, lang) ? ' lang="' + LANGS[lang] + '"' : "";
  }
  function times(count) {
    return count && count > 1 ? '<em class="times">× ' + esc(count) + "</em>" : "";
  }

  var BLOCKS = {
    heading: function (b) { return "<h2>" + esc(b.text) + "</h2>"; },
    text: function (b) { return '<div class="text">' + markdown(b.text) + "</div>"; },
    callout: function (b) { return '<div class="callout"><span class="who">Yay</span><p>' + esc(b.text) + "</p></div>"; },
    stats: function (b) {
      return '<div class="stats n' + b.items.length + '">' + b.items.map(function (i) { return '<div class="stat"><b>' + esc(i.value) + "</b><small>" + esc(i.label) + "</small></div>"; }).join("") + "</div>";
    },
    chart: chart,
    calendar: calendar,
    ring: ring,
    rhythm: function (b) {
      return (b.title ? "<h3>" + esc(b.title) + "</h3>" : "") + '<div class="rhythm">' + b.steps.map(function (st) { return '<div><b>' + esc(st.seconds) + "</b><small>" + esc(st.label) + "</small></div>"; }).join("") + "</div>" + (b.note ? '<p class="note">' + esc(b.note) + "</p>" : "");
    },
    sessions: function (b) {
      return (b.title ? "<h3>" + esc(b.title) + "</h3>" : "") + '<div class="sessions">' + b.groups.map(function (g) {
        return '<div class="week"><div class="week-head"' + openAttr(g.open) + "><b>" + esc(g.title) + "</b>" + (g.summary ? "<small>" + esc(g.summary) + "</small>" : "") + (g.open ? '<span class="go">›</span>' : "") + "</div>" +
          (g.items.length ? '<div class="rows">' + g.items.map(function (i) {
            return '<div class="row"' + openAttr(i.open) + "><span>" + esc(i.title) + "</span>" + (i.note ? "<small>" + esc(i.note) + "</small>" : "") + (i.open ? '<span class="go">›</span>' : "") + "</div>";
          }).join("") + "</div>" : "") + "</div>";
      }).join("") + "</div>";
    },
    list: function (b) {
      return (b.title ? '<h3>' + esc(b.title) + "</h3>" : "") + '<div class="rows">' + b.items.map(function (i) { return '<div class="row"><span>' + esc(i.title) + "</span>" + (i.note ? "<small>" + esc(i.note) + "</small>" : "") + "</div>"; }).join("") + "</div>";
    },
    table: function (b) {
      return '<div class="table"><table><thead><tr>' + b.columns.map(function (c) { return "<th>" + esc(c) + "</th>"; }).join("") + "</tr></thead><tbody>" +
        b.rows.map(function (r) { return "<tr>" + r.map(function (c) { return "<td>" + esc(c) + "</td>"; }).join("") + "</tr>"; }).join("") + "</tbody></table></div>";
    },
    timeline: function (b) {
      return '<div class="timeline">' + b.items.map(function (i) { return '<div class="tl"><time>' + esc(i.time) + "</time><p>" + esc(i.text) + "</p></div>"; }).join("") + "</div>";
    },
    words: function (b) {
      return (b.title ? "<h3>" + esc(b.title) + "</h3>" : "") + '<div class="rows words">' + b.items.map(function (i) {
        return '<div class="word"' + langAttr(i.lang) + "><b>" + esc(i.word) + times(i.count) + "</b>" + (i.meaning ? "<span>" + esc(i.meaning) + "</span>" : "") + (i.example ? "<small>" + esc(i.example) + "</small>" : "") + "</div>";
      }).join("") + "</div>";
    },
    compare: function (b) {
      return "<h3>" + esc(b.title || "更好的说法") + '</h3><div class="compare">' + b.items.map(function (i) {
        return '<div class="cmp"' + langAttr(i.lang) + "><s>" + esc(i.said) + "</s><span>" + marked(i.said, i.better) + times(i.count) + "</span>" + (i.note ? "<small>" + esc(i.note) + "</small>" : "") + "</div>";
      }).join("") + "</div>";
    },
    hanzi: function (b) {
      return '<div class="hanzi">' + b.items.map(function (i) {
        return '<div class="hz"><span class="tzg">' + esc(i.char) + "</span><small>" + esc(i.pinyin || "") + (i.count && i.count > 1 ? " · " + i.count + " 次" : "") + "</small>" + (i.words && i.words.length ? "<em>" + i.words.map(esc).join(" ") + "</em>" : "") + "</div>";
      }).join("") + "</div>";
    },
    links: function (b) {
      return '<div class="rows links">' + b.items.map(function (i) {
        var href = safeUrl(i.url);
        var body = '<span class="lk">↗</span><span class="lt">' + esc(i.title) + "<small>" + esc([i.site, i.note].filter(Boolean).join(" · ")) + "</small></span>";
        return href ? '<a class="link" href="' + esc(href) + '" data-link>' + body + "</a>" : '<div class="link">' + body + "</div>";
      }).join("") + "</div>";
    },
  };

  /**
   * The page as HTML: its head, its blocks and where it came from. Unknown blocks are left out, and so is a
   * block this renderer cannot draw, so one bad block never blanks the page.
   */
  function render(spec) {
    if (!spec || spec.spec !== "yay.page/v1" || !Array.isArray(spec.blocks)) return '<p class="empty">这个页面打不开。</p>';
    var service = spec.service && typeof spec.service === "object" && spec.service.name ? spec.service : null;
    var html =
      '<header class="head">' +
      (service ? serviceHead(service) + headed(spec, service) : "<h1>" + esc(spec.title) + "</h1>" + (spec.subtitle ? "<p>" + esc(spec.subtitle) + "</p>" : "")) +
      "</header>";
    spec.blocks.forEach(function (block) {
      var draw = block && Object.prototype.hasOwnProperty.call(BLOCKS, block.type) ? BLOCKS[block.type] : null;
      if (!draw) return;
      var inner;
      try {
        inner = draw(block);
      } catch (_error) {
        return;
      }
      // "b-" keeps the section's class apart from the block's own (.stats, .chart, .table…).
      html += '<section class="blk b-' + esc(block.type) + '">' + inner + "</section>";
    });
    if (spec.sources && spec.sources.length)
      html += '<footer class="sources">来源：' + spec.sources.map(esc).join("；") + "</footer>";
    return html;
  }

  /** Shows a page in this document; links go to the app (iOS) or the parent page (web) to open outside. */
  function show(spec, options) {
    options = options || {};
    var doc = root.document;
    if (options.theme === "dark" || options.theme === "light") doc.documentElement.setAttribute("data-theme", options.theme);
    doc.getElementById("page").innerHTML = render(spec);
    doc.title = spec && spec.title ? String(spec.title) : "页面";
  }

  if (root.document)
    root.document.addEventListener("click", function (event) {
      var bridge = root.webkit && root.webkit.messageHandlers && root.webkit.messageHandlers.yay;
      // A history row: the app opens that day's or week's page (GET /v1/pages/:id/history/:ref).
      var row = event.target.closest && event.target.closest("[data-open]");
      if (row) {
        var ref = row.getAttribute("data-open");
        if (!/^(day|week):\d{4}-\d{2}-\d{2}$/.test(String(ref))) return;
        if (bridge) bridge.postMessage({ type: "history", ref: ref });
        else if (root.parent && root.parent !== root) root.parent.postMessage({ type: "yay-page-history", ref: ref }, "*");
        return;
      }
      var link = event.target.closest && event.target.closest("[data-link]");
      if (!link) return;
      event.preventDefault();
      var href = safeUrl(link.getAttribute("href"));
      if (!href) return;
      if (bridge) bridge.postMessage({ type: "open", url: href });
      else if (root.parent && root.parent !== root) root.parent.postMessage({ type: "yay-page-open", url: href }, "*");
    });

  root.YayPage = { render: render, show: show };
})(typeof window !== "undefined" ? window : globalThis);
