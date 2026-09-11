(() => {
  const ignored = new Set(["wiki/Footer.html", "wiki/Sidebar.html"]);
  const idx = (window.TATARUS_SEARCH_INDEX || []).filter(item => !ignored.has(item.url || item.href));
  const input = document.querySelector("#doc-search, [data-doc-search]");
  const out = document.querySelector("#search-results, [data-search-results]");
  if (!input || !out) return;
  const norm = s => (s || "").toLocaleLowerCase("de-DE");
  input.addEventListener("input", () => {
    const q = norm(input.value.trim());
    out.innerHTML = "";
    if (q.length < 2) return;
    const terms = q.split(/\s+/).filter(Boolean);
    const scored = [];
    for (const item of idx) {
      const hay = norm(`${item.title || ""} ${item.group || ""} ${item.text || ""}`);
      if (!terms.every(term => hay.includes(term))) continue;
      const title = norm(item.title || "");
      let score = 0;
      for (const term of terms) {
        if (title.includes(term)) score += 5;
        if (hay.startsWith(term)) score += 2;
      }
      scored.push([score, item]);
    }
    scored.sort((a,b) => b[0] - a[0] || (a[1].title || "").localeCompare(b[1].title || "", "de"));
    for (const [, item] of scored.slice(0, 20)) {
      const a = document.createElement("a");
      a.className = "search-result";
      a.href = item.url || item.href;
      const b = document.createElement("b");
      b.textContent = item.title || "Dokument";
      const span = document.createElement("span");
      span.textContent = (item.text || "").slice(0, 190);
      a.append(b, span);
      out.append(a);
    }
  });
})();
