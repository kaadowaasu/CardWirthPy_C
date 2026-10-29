// 既存の /user マウント内の Editor のファイル選択。アップロード、ダウンロード
// 、ブラウザーのファイル入力、新たな File System Access 権限は使わない。
(function (global) {
  "use strict";
  const root = "/user";
  const withinRoot = path => path === root || path.startsWith(root + "/");
  const normalize = value => {
    const parts = [];
    for (const part of String(value || "").replace(/\\/g, "/").split("/")) {
      if (!part || part === ".") continue;
      if (part === "..") parts.pop(); else parts.push(part);
    }
    return "/" + parts.join("/");
  };
  const basename = path => path.slice(path.lastIndexOf("/") + 1);
  const parent = path => path.slice(0, path.lastIndexOf("/")) || "/";
  const validName = name => !!name && name !== "." && name !== ".." &&
    !/[\\/\0-\x1f]/.test(name);

  function createModel(fs, options = {}) {
    const mode = Number(options.mode || 0);
    if (![0, 1, 2].includes(mode)) throw new Error("不明な選択操作です");
    const resolve = path => {
      path = normalize(path);
      if (!withinRoot(path)) throw new Error("UserDataの外は選択できません");
      const resolved = normalize(fs.lookupPath(path, {follow: true}).path);
      if (!withinRoot(resolved)) throw new Error("UserData外へのリンクは選択できません");
      return resolved;
    };
    const isDirectory = path => fs.isDir(fs.stat(path).mode);
    let directory = root;
    let name = "";
    const hint = String(options.initial || root).replace(/\\/g, "/");
    const initial = normalize(hint.startsWith("/") || /^[a-z]:/i.test(hint)
      ? hint : root + "/" + hint);
    if (withinRoot(initial)) {
      let candidate = initial;
      try {
        const resolved = resolve(candidate);
        if (!isDirectory(resolved)) { name = basename(candidate); candidate = parent(candidate); }
      } catch (_) {
        name = basename(candidate);
        candidate = parent(candidate);
      }
      while (withinRoot(candidate)) {
        try { const resolved = resolve(candidate); if (isDirectory(resolved)) { directory = resolved; break; } }
        catch (_) { /* 記憶したディレクトリーがすでに存在しない場合がある。 */ }
        if (candidate === root) break;
        candidate = parent(candidate);
      }
    }
    const filters = Array.isArray(options.filters) ? options.filters : [];
    let selectedFilter = filters.length ? (options.selectedFilter ?? 0) : undefined;
    if (selectedFilter !== undefined && (!Number.isInteger(selectedFilter) ||
        selectedFilter < 0 || selectedFilter >= filters.length)) throw new Error("保存形式が不正です");
    const patterns = () => String(filters[selectedFilter]?.extensions ?? options.extensions ?? '')
      .toLowerCase().split(';').filter(Boolean);
    const defaultSuffix = () => {
      const pattern = patterns()[0] || '';
      const suffix = pattern.slice(pattern.lastIndexOf('.'));
      return pattern.includes('.') && /^\.[a-z0-9]+$/i.test(suffix) ? suffix : '';
    };
    const matches = name => {
      const entries = patterns();
      return !entries.length || entries.some(pattern => pattern === '*' || pattern === '*.*' ||
        (pattern.startsWith('*.') ? name.toLowerCase().endsWith(pattern.slice(1)) :
          pattern.includes('.') ? name.toLowerCase() === pattern : name.toLowerCase().endsWith('.' + pattern)));
    };
    const model = {
      mode, name, selected: new Set(),
      filters,
      get selectedFilter() { return selectedFilter; },
      setFilter(index) {
        if (!Number.isInteger(index) || index < 0 || index >= filters.length)
          throw new Error("保存形式が不正です");
        const previousSuffix = defaultSuffix();
        selectedFilter = index;
        const suffix = defaultSuffix();
        if (mode === 2 && suffix && previousSuffix &&
            model.name.toLowerCase().endsWith(previousSuffix))
          model.name = model.name.slice(0, -previousSuffix.length) + suffix;
        model.selected.clear();
      },
      get directory() { return directory; },
      // 現在フォルダーにフィルターで許可する Summary があれば、開く画面で
      // Summary.xml、次に Summary.wsm の順に事前選択する。
      autoSelection(entries) {
        if (mode !== 0) return null;
        for (const summary of ["summary.xml", "summary.wsm"]) {
          const entry = entries.find(item => !item.directory &&
            item.name.toLowerCase() === summary && matches(item.name));
          if (entry) return entry;
        }
        return null;
      },
      list() {
        return fs.readdir(directory).filter(value => value !== "." && value !== "..")
          .flatMap(value => {
            try {
              const path = resolve(directory + "/" + value);
              const folder = isDirectory(path);
              if (!folder && (mode === 1 || !matches(value))) return [];
              return [{name: value, path, directory: folder}];
            } catch (_) { return []; } // アクセス不能、またはルート外へのリンク。
          }).sort((a, b) => Number(b.directory) - Number(a.directory) || a.name.localeCompare(b.name));
      },
      enter(path) {
        const resolved = resolve(path);
        if (!isDirectory(resolved)) throw new Error("フォルダーを選択してください");
        directory = resolved;
        model.selected.clear();
      },
      up() { if (directory !== root) model.enter(parent(directory)); },
      select(paths) {
        if (!options.multiple && paths.length > 1) throw new Error("1件だけ選択してください");
        model.selected = new Set(paths.map(resolve));
        if (paths.length === 1 && !isDirectory(paths[0])) model.name = basename(paths[0]);
      },
      accept(overwrite = false) {
        if (mode === 1) {
          const chosen = model.selected.size ? [...model.selected][0] : directory;
          const path = resolve(chosen);
          if (!isDirectory(path)) throw new Error("フォルダーを選択してください");
          return {paths: [path]};
        }
        if (mode === 2) {
          if (!validName(model.name)) throw new Error("有効なファイル名を入力してください");
          // 上書き確認より前に既定の拡張子を解決する。別の出力
          // 先を確認した後で、書き込み側が拡張子を補わない。
          if (!model.name.includes('.')) model.name += defaultSuffix();
          const path = resolve(directory) + "/" + model.name;
          let exists = false;
          try { fs.stat(path); exists = true; }
          catch (error) { if (error.code !== "ENOENT" && error.errno !== 44) throw error; }
          if (exists) {
            const resolved = resolve(path);
            if (isDirectory(resolved)) throw new Error("同名のフォルダーがあります");
            if (options.confirmOverwrite !== false && !overwrite) return {overwrite: resolved};
            return {paths: [resolved]};
          }
          return {paths: [path]}; // 選択ではファイルを作成・切り詰めない。
        }
        const paths = model.selected.size ? [...model.selected] :
          validName(model.name) ? [directory + "/" + model.name] : [];
        if (!paths.length) throw new Error("ファイルを選択してください");
        return {paths: paths.map(value => {
          const path = resolve(value);
          if (isDirectory(path)) throw new Error("フォルダーはダブルクリックで開いてください");
          return path;
        })};
      }
    };
    return model;
  }

  function choose(fs, options) {
    const model = createModel(fs, options);
    return new Promise(resolve => {
      const previousFocus = document.activeElement;
      const overlay = document.createElement("div");
      overlay.style.cssText = "position:fixed;inset:0;z-index:2147483647;background:#0009;display:grid;place-items:center;font:16px sans-serif;color:#111";
      const panel = document.createElement("form");
      panel.setAttribute("role", "dialog"); panel.setAttribute("aria-modal", "true");
      panel.setAttribute("aria-label", options.title || "UserDataから選択");
      panel.style.cssText = "background:#eee;padding:16px;width:min(680px,90vw);max-height:90vh;display:grid;gap:10px;box-sizing:border-box";
      const heading = document.createElement("strong"); heading.textContent = options.title || "UserDataから選択";
      const location = document.createElement("div"); location.style.overflowWrap = "anywhere";
      const up = document.createElement("button"); up.type = "button"; up.textContent = "上のフォルダー";
      const list = document.createElement("select"); list.size = 12; list.multiple = !!options.multiple && model.mode === 0;
      list.setAttribute("aria-label", "UserDataのファイルとフォルダー");
      list.style.cssText = "font:inherit;min-height:120px;height:min(40vh,320px);width:100%";
      const filename = document.createElement("input"); filename.value = model.name;
      filename.setAttribute("aria-label", "ファイル名"); filename.placeholder = "ファイル名";
      filename.hidden = model.mode === 1; filename.style.font = "inherit";
      const error = document.createElement("div"); error.setAttribute("role", "status"); error.style.color = "#a00000";
      const format = document.createElement("select");
      format.setAttribute("aria-label", "ファイル形式");
      format.style.cssText = "font:inherit;font-size:16px;min-height:48px;padding:8px 12px;width:100%;box-sizing:border-box";
      format.hidden = !model.filters.length || model.mode === 1;
      model.filters.forEach((filter, index) => {
        const option = document.createElement("option");
        option.value = String(index); option.textContent = filter.label;
        format.append(option);
      });
      if (model.selectedFilter !== undefined) format.value = String(model.selectedFilter);
      const accept = document.createElement("button"); accept.type = "submit";
      const label = model.mode === 2 ? "保存" : model.mode === 1 ? "このフォルダーを選択" : "開く";
      accept.textContent = label;
      const cancel = document.createElement("button"); cancel.type = "button"; cancel.textContent = "キャンセル";
      let overwritePath = null;
      const changed = () => { overwritePath = null; accept.textContent = label; error.textContent = ""; };
      const render = () => {
        location.textContent = model.directory;
        up.disabled = model.directory === root;
        list.replaceChildren();
        const entries = model.list();
        for (const entry of entries) {
          const option = document.createElement("option"); option.value = entry.path;
          option.textContent = (entry.directory ? "📁 " : "") + entry.name;
          option.dataset.directory = entry.directory ? "1" : "0";
          list.append(option);
        }
        list.selectedIndex = -1;
        const auto = model.autoSelection(entries);
        if (auto) {
          const index = entries.indexOf(auto);
          model.select([auto.path]);
          list.selectedIndex = index;
          filename.value = model.name;
        }
        changed();
      };
      const attempt = action => { try { action(); } catch (failure) { error.textContent = failure.message || String(failure); } };
      let finished = false;
      const finish = paths => {
        if (finished) return;
        finished = true;
        document.removeEventListener("keydown", blockOutside, true);
        document.removeEventListener("keyup", blockOutside, true);
        overlay.remove();
        // 無効・終了済みの IME 対象を復元するとき、ブラウザーや埋め込み側のフォ
        // ーカスフックが例外を投げても、呼び出し元のモーダル待機を残してはならない。
        try { previousFocus?.focus?.(); } catch (_) { /* 以前の所有者が閉じている場合がある。 */ }
        resolve(paths ? {paths, selectedFilter: model.selectedFilter} : null);
      };
      const blockOutside = event => {
        if (!overlay.contains(event.target)) { event.preventDefault(); event.stopImmediatePropagation(); }
      };
      overlay.addEventListener("keydown", event => {
        event.stopPropagation();
        if (event.key === "Escape") { event.preventDefault(); finish(null); }
        if (event.key === "Tab") {
          const controls = [...panel.querySelectorAll("button,input,select")].filter(node => !node.disabled && !node.hidden);
          const index = controls.indexOf(document.activeElement);
          if ((!event.shiftKey && index === controls.length - 1) || (event.shiftKey && index <= 0)) {
            event.preventDefault(); controls[event.shiftKey ? controls.length - 1 : 0].focus();
          }
        }
      });
      overlay.addEventListener("keyup", event => event.stopPropagation());
      cancel.onclick = () => finish(null);
      up.onclick = () => attempt(() => { model.up(); render(); });
      format.onchange = () => attempt(() => {
        model.setFilter(Number(format.value)); filename.value = model.name; render();
      });
      list.onchange = () => attempt(() => {
        model.select([...list.selectedOptions].map(option => option.value));
        filename.value = model.name; changed();
      });
      list.ondblclick = () => attempt(() => {
        const selected = list.selectedOptions[0];
        if (selected?.dataset.directory === "1") { model.enter(selected.value); render(); }
        else panel.requestSubmit();
      });
      list.addEventListener("keydown", event => {
        if (event.key === "Enter") { event.preventDefault(); panel.requestSubmit(); }
      });
      filename.oninput = () => { model.name = filename.value; model.selected.clear(); changed(); };
      panel.onsubmit = event => {
        event.preventDefault();
        attempt(() => {
          const selected = list.selectedOptions[0];
          if (model.mode !== 1 && selected?.dataset.directory === "1" && model.selected.size) {
            model.enter(selected.value); render(); return;
          }
          const result = model.accept(false);
          if (result.overwrite && overwritePath !== result.overwrite) {
            overwritePath = result.overwrite; accept.textContent = "上書き保存";
            error.textContent = "同名のファイルがあります。上書きする場合は「上書き保存」を押してください。";
            return;
          }
          finish(result.overwrite ? model.accept(true).paths : result.paths);
        });
      };
      render(); // マウントが利用できなければ、モーダル用の監視を登録する前に失敗する。
      panel.append(heading, location, up, list, filename, format, error, accept, cancel);
      overlay.append(panel); document.body.append(overlay);
      document.addEventListener("keydown", blockOutside, true);
      document.addEventListener("keyup", blockOutside, true);
      list.focus();
    });
  }
  global.CardWirthStoragePicker = Object.freeze({createModel, choose});
})(globalThis);
