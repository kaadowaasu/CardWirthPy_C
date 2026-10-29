// 配布専用の拡張。Pages の個別ファイルサイズ制限に対応し、pthread も
// 含む Emscripten の起動前に、リンク済みバイナリーを正確に再結合する。
(function (global) {
  "use strict";
  const CACHE_DB_NAME = "CardWirthPy_CWebWasm";
  const CACHE_DB_VERSION = 1;
  const CACHE_STORE = "modules";
  const CACHE_KEY = "current";
  const hex = (bytes) => Array.from(new Uint8Array(bytes), b => b.toString(16).padStart(2, "0")).join("");
  let cacheDbPromise;

  const requestResult = (request) => new Promise((resolve, reject) => {
    request.onsuccess = () => resolve(request.result);
    request.onerror = () => reject(request.error || new Error("WASM IndexedDB request failed"));
  });

  const transactionDone = (transaction) => new Promise((resolve, reject) => {
    transaction.oncomplete = () => resolve();
    transaction.onerror = () => reject(transaction.error || new Error("WASM IndexedDB transaction failed"));
    transaction.onabort = () => reject(transaction.error || new Error("WASM IndexedDB transaction aborted"));
  });

  const openCacheDb = () => {
    if (typeof global.indexedDB === "undefined") return Promise.resolve(null);
    if (!cacheDbPromise) {
      cacheDbPromise = new Promise((resolve, reject) => {
        const request = global.indexedDB.open(CACHE_DB_NAME, CACHE_DB_VERSION);
        request.onupgradeneeded = () => {
          if (!request.result.objectStoreNames.contains(CACHE_STORE))
            request.result.createObjectStore(CACHE_STORE, {keyPath: "key"});
        };
        request.onsuccess = () => {
          const database = request.result;
          database.onversionchange = () => database.close();
          resolve(database);
        };
        request.onerror = () => reject(request.error || new Error("WASM IndexedDBを開けません。"));
        request.onblocked = () => reject(new Error("WASM IndexedDBが別のタブで使用中です。"));
      }).catch((error) => {
        cacheDbPromise = undefined;
        throw error;
      });
    }
    return cacheDbPromise;
  };

  const readCache = async (manifest) => {
    let database;
    try { database = await openCacheDb(); }
    catch (_) { return null; }
    if (!database) return null;
    try {
      const transaction = database.transaction(CACHE_STORE, "readonly");
      const record = await requestResult(transaction.objectStore(CACHE_STORE).get(CACHE_KEY));
      if (!record || record.sha256 !== manifest.sha256 || record.size !== manifest.size || !record.binary)
        return null;
      const bytes = typeof global.Blob === "function" && record.binary instanceof global.Blob
        ? new Uint8Array(await record.binary.arrayBuffer())
        : new Uint8Array(record.binary);
      if (bytes.length !== manifest.size || hex(await global.crypto.subtle.digest("SHA-256", bytes)) !== manifest.sha256)
        return null;
      return bytes;
    } catch (_) {
      return null;
    }
  };

  const writeCache = async (manifest, binary) => {
    const database = await openCacheDb();
    if (!database) throw new Error("このブラウザはIndexedDBに対応していません。");
    const transaction = database.transaction(CACHE_STORE, "readwrite");
    transaction.objectStore(CACHE_STORE).put({
      key: CACHE_KEY,
      sha256: manifest.sha256,
      size: manifest.size,
      binary: typeof global.Blob === "function"
        ? new global.Blob([binary], {type: "application/wasm"}) : binary,
      updatedAt: Date.now()
    });
    await transactionDone(transaction);
  };

  const uninstall = async () => {
    if (typeof global.indexedDB === "undefined") return;
    let database = null;
    try { database = await openCacheDb(); }
    catch (_) { cacheDbPromise = undefined; }
    if (database) database.close();
    cacheDbPromise = undefined;
    await new Promise((resolve, reject) => {
      const request = global.indexedDB.deleteDatabase(CACHE_DB_NAME);
      request.onsuccess = () => resolve();
      request.onerror = () => reject(request.error || new Error("WASM IndexedDBの削除に失敗しました。"));
      request.onblocked = () => reject(new Error(
        "WASM IndexedDBを使用中のタブがあります。ほかのCardWirthPy_C Webタブを閉じて再実行してください。"));
    });
  };

  const fetchManifest = async (manifestUrl) => {
    const url = new URL(manifestUrl, global.location?.href);
    const response = await fetch(url);
    if (!response.ok) throw new Error(`WASM manifest HTTP ${response.status}`);
    const manifest = await response.json();
    const digestPattern = /^[0-9a-f]{64}$/;
    if (manifest.format !== 1 || !Number.isSafeInteger(manifest.size) ||
        manifest.size <= 0 || manifest.size > 1024 * 1024 * 1024 ||
        !digestPattern.test(manifest.sha256) || !Array.isArray(manifest.parts) ||
        !manifest.parts.length || manifest.parts.length > 256)
      throw new Error("Invalid WASM manifest");
    let total = 0;
    const names = new Set();
    for (const part of manifest.parts) {
      if (!/^[A-Za-z0-9_.-]+\.part$/.test(part.file) || names.has(part.file) ||
          !Number.isSafeInteger(part.size) || part.size <= 0 || part.size > 20 * 1024 * 1024 ||
          !digestPattern.test(part.sha256)) throw new Error("Invalid WASM part");
      names.add(part.file);
      total += part.size;
    }
    if (total !== manifest.size) throw new Error("WASM manifest size mismatch");
    return {url, manifest};
  };

  const loadParts = async (url, manifest, progress) => {
    const binary = new Uint8Array(manifest.size);
    let offset = 0;
    // 順番に取得して、余分なメモリーを一片分に抑える。全片に加えて
    // Blob と全体の ArrayBuffer を同時保持しない。
    for (const [index, part] of manifest.parts.entries()) {
      progress(index, manifest.parts.length);
      const partUrl = new URL(part.file, url);
      partUrl.search = url.search;
      const result = await fetch(partUrl);
      if (!result.ok) throw new Error(`WASM part ${index + 1}: HTTP ${result.status}`);
      const bytes = new Uint8Array(await result.arrayBuffer());
      if (bytes.length !== part.size || hex(await global.crypto.subtle.digest("SHA-256", bytes)) !== part.sha256)
        throw new Error(`WASM part ${index + 1}: size/hash mismatch`);
      binary.set(bytes, offset);
      offset += bytes.length;
    }
    if (hex(await global.crypto.subtle.digest("SHA-256", binary)) !== manifest.sha256)
      throw new Error("WASM combined hash mismatch");
    progress(manifest.parts.length, manifest.parts.length);
    return binary;
  };

  async function load(manifestUrl, progress = () => {}, options = {}) {
    const {url, manifest} = await fetchManifest(manifestUrl);
    if (options.useCache !== false && !options.forceNetwork) {
      const cached = await readCache(manifest);
      if (cached) {
        progress(manifest.parts.length, manifest.parts.length);
        return cached;
      }
    }
    if (options.requireCache)
      throw new Error("Py_CプログラムがIndexedDBにありません。起動画面からインストールしてください。");
    return loadParts(url, manifest, progress);
  }

  async function isInstalled(manifestUrl) {
    const {manifest} = await fetchManifest(manifestUrl);
    return Boolean(await readCache(manifest));
  }

  async function install(manifestUrl, progress = () => {}) {
    const {url, manifest} = await fetchManifest(manifestUrl);
    const binary = await loadParts(url, manifest, progress);
    await writeCache(manifest, binary);
    return {size: manifest.size, sha256: manifest.sha256, parts: manifest.parts.length};
  }

  async function prepare(manifestUrl, progress) {
    const binary = await load(manifestUrl, progress, {requireCache: true});
    const module = await WebAssembly.compile(binary);
    // 生成ラッパーの既定 API は instantiateWasm に対応するが wasmBinary
    // には対応しない。準備後はコンパイル済みモジュールだけを保持する。
    let rejectFailure;
    const failure = new Promise((_, reject) => { rejectFailure = reject; });
    return {failure, instantiateWasm(imports, receive) {
      // Chromium はブラウザーメイン上で大きなモジュールの同
      // 期生成を禁止する。非同期のエラーは起動ページへ伝播させる。
      WebAssembly.instantiate(module, imports)
        .then(instance => receive(instance, module))
        .catch(rejectFailure);
      return {};
    }};
  }
  global.CardWirthWasmParts = {load, install, uninstall, isInstalled, prepare};
})(globalThis);
