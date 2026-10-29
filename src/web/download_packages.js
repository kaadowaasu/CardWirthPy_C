(function(global) {
  "use strict";
  // Web 固有の追加機能。無関係な UserData を置き換えず、配信パッケージを導入する。
  const clean = value => {
    if (typeof value !== "string" || !value || value.includes("\\") || value.includes(":" ) || value.includes("\0") ||
        value.split("/").some(p => !p || p === "." || p === "..")) throw new Error("不正な配布パスです。");
    return value.normalize("NFC");
  };
  async function install(group, baseUrl, storage, progress, onCommitted = null) {
    if (!["basic","scenarios","faces"].includes(group)) throw new Error("不正なデータ分類です。");
    const base = new URL(baseUrl);
    const get = async file => {
      const relative = clean(file).split("/").map(encodeURIComponent).join("/");
      const response = await fetch(new URL(relative, base));
      if (!response.ok) throw new Error(`${file}: HTTP ${response.status}`);
      return response;
    };
    const manifest = await (await get("manifest.json")).json();
    const packages = manifest[group];
    if (manifest.format !== 1 || !Array.isArray(packages) || !packages.length) throw new Error("配布一覧が未配置、または空です。");
    const entries = [];
    let expanded = 0;
    const add = (path, data) => {
      expanded += data.size;
      if (entries.length >= 20000 || data.size > 256*1024*1024 || expanded > 512*1024*1024)
        throw new Error("展開するデータが上限を超えています。");
      entries.push({path:`/user/${clean(path)}`, data});
    };
    for (const [i, item] of packages.entries()) {
      const target = clean(item.target);
      if (!/^(Data|Scenario|Yado|System)(\/|$)/.test(target)) throw new Error("不正な配置先です。");
      if (group === "scenarios" && !target.startsWith("Scenario/") ||
          group === "faces" && target !== "Data/Face") throw new Error("データ分類と配置先が一致しません。");
      progress(`${i+1}/${packages.length} ${item.file}`);
      const data = await (await get(item.file)).blob();
      if (data.size > 256*1024*1024) throw new Error("ダウンロードサイズが上限を超えています。");
      if (item.kind === "file") { add(target, data); continue; }
      if (item.kind !== "zip") throw new Error("不正な配布形式です。");
      const zip = await global.JSZip.loadAsync(data, {decodeFileName: bytes => {
        try { return new TextDecoder("utf-8", {fatal:true}).decode(bytes); }
        catch (_) { return new TextDecoder("shift-jis", {fatal:true}).decode(bytes); }
      }});
      for (const entry of Object.values(zip.files)) {
        if (entry.dir) continue;
        let name = clean(entry.unsafeOriginalName || entry.name);
        for (const root of item.roots || []) {
          const prefix = clean(root) + "/";
          if (name.toLowerCase().startsWith(prefix.toLowerCase())) { name = name.slice(prefix.length); break; }
        }
        if (Number(entry._data?.uncompressedSize || 0) > 256*1024*1024) throw new Error("展開サイズが上限を超えています。");
        add(`${target}/${name}`, await entry.async("blob"));
      }
    }
    progress("IndexedDBへ保存しています。");
    const count = await storage.mergeDownloadFiles(entries);
    // 動作中の ALL に、確定済みのファイルを直ちに見せる。起動
    // ページと同じ取得・保存経路を使い、実行時コールバックは内容を
    // 読まず、既存の共通遅延 FS にメタデータだけを再適用する。
    if (onCommitted) await onCommitted(entries.map(entry => entry.path));
    return count;
  }
  const confirmBasic = (ask) => ask("既存の基本データと同じパスのファイルを上書きします。宿のデータも含まれます。\n続ける場合は「基本データ」と入力してください。") === "基本データ";
  const confirmDelete = (ask) => ask("確認1/2: IndexedDBの全データを削除します。\n続ける場合は「削除」と入力してください。") === "削除" &&
    ask("最終確認2/2: 宿・キャラクター・設定も削除され、元には戻せません。\n実行する場合は「全削除」と入力してください。") === "全削除";
  global.CardWirthDownloadPackages = {install, confirmBasic, confirmDelete};
})(globalThis);
