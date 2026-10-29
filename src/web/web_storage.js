(function initializeCardWirthWebStorage(global) {
  "use strict";

  const databaseSuffix = String(global.CardWirthWebStorageDatabaseSuffix || "")
    .replace(/[^A-Za-z0-9_.-]/g, "").slice(0, 48);
  // バージョン 1 は Engine・Editor・Data Explorer の
  // Web データアクセスを一つの DB に統一する。内容と stat は別の保
  // 存領域にし、ディレクトリー・stat だけの操作で Blob を取得しない。
  const STORAGE_DB_NAME = `CardWirthPy_CWebStorage${databaseSuffix}`;
  const STORAGE_DB_VERSION = 1;
  const FILE_STORE = "files";
  const STAT_STORE = "stats";
  const SETTINGS_STORE = "settings";
  // UserData 以外のバイナリー素材も内容の保存領
  // 域を共有するが、独自の範囲に分ける。UserData
  // の統計やゲームファイルの書き出しには含めない。
  const PERSISTENT_ASSET_SCOPE = "asset";
  const PERSISTENT_ASSET_MAX_BYTES = 64 * 1024 * 1024;
  const LEGACY_DATABASES = Object.freeze([
    {name: `CardWirthCWebFiles${databaseSuffix}`, store: FILE_STORE},
    {name: `CardWirthCWebStats${databaseSuffix}`, store: STAT_STORE},
    {name: `CardWirthCWebSettings${databaseSuffix}`, store: SETTINGS_STORE}
  ]);
  const LEGACY_MIGRATION_KEY = "legacy-split-database-migration-v1";
  const MODE_KEY = "storage-mode";
  const DIRECTORY_HANDLE_KEY = "filesystem-directory-handle";
  const UPLOAD_READY_KEY = "userdata-upload-ready-v1";
  const TRANSIENT_NAMES = new Set([".CardWirthPy.lock"]);
  const IMPORT_LIMITS = Object.freeze({
    maxArchiveBytes: 512 * 1024 * 1024,
    maxEntries: 10000,
    maxFileBytes: 128 * 1024 * 1024,
    maxTotalBytes: 768 * 1024 * 1024,
    maxCompressionRatio: 200,
    maxPathCharacters: 1024,
    quotaReserveBytes: 16 * 1024 * 1024
  });

  let storageDbPromise;
  let initializationPromise;
  // 保存モードはセッション単位の選択。遅延ファイル検索ごとに
  // 設定を読むと、大きなシナリオでファイルごとに IndexedDB
  // のトランザクションが増えるため、解決済みモードをキ
  // ャッシュする。設定変更・DB 破棄時だけ無効にする。
  let resolvedStorageMode;
  let resolvedFileSystemDirectory;
  // Engine 初期化と HTML の保存領域操作は同じページで動
  // き、両方が stat 索引を使う。書き込みの間は索引が不変なので
  // 、Blob を含まない正規化済みメタデータを保持し、二つ目の利用
  // 側が全件 getAll とパス正規化を繰り返さないようにする。
  const runtimeStatsCache = new Map();
  // Blob レコードのキャッシュは内容読み取り・先読みだけに使い、stat
  // 検索には使わない。DataLoader は別の範囲キャッシュでバイト
  // を読む。IndexedDB の Blob の実際のメモリー確保はブラ
  // ウザー次第なので、取得にファイル全体分のコストがないとは仮定しない。
  const runtimeFileCache = new Map();
  // 32 MiB/2 MiB の上限で内容レコードの保持量を制限する。共通
  // のスキン・システム素材を再利用し、大きなメディアは遅延読み込みに残す。
  const RUNTIME_FILE_CACHE_MAX_BYTES = 32 * 1024 * 1024;
  const RUNTIME_FILE_CACHE_MAX_FILE_BYTES = 2 * 1024 * 1024;
  const RUNTIME_FILE_BATCH_MAX_FILES = 512;
  let runtimeFileCacheBytes = 0;
  let runtimeFileCacheGeneration = 0;
  let runtimeFileBatch = new Map();
  let runtimeFileBatchScheduled = false;

  const invalidateRuntimeStats = (scope = null) => {
    if (scope === null) runtimeStatsCache.clear();
    else runtimeStatsCache.delete(scope);
  };

  const clearRuntimeFileCache = () => {
    runtimeFileCacheGeneration++;
    runtimeFileCache.clear();
    runtimeFileCacheBytes = 0;
  };

  const invalidateRuntimeFiles = (keys) => {
    // 進行中の先読みを無効化しつつ、設定・保存ファイルが変わっても無
    // 関係な取得済み素材は保つ。一回の保存でスキン全体を破棄しない。
    runtimeFileCacheGeneration++;
    for (const key of keys) {
      const previous = runtimeFileCache.get(key);
      if (previous && previous.blob) runtimeFileCacheBytes -= previous.blob.size;
      runtimeFileCache.delete(key);
    }
  };

  const invalidateRuntimeFileTrees = (scope, paths) => {
    const prefixes = paths.map((path) => `${scope}:${lowerPath(path).replace(/\/$/, "")}`);
    const affected = [];
    for (const key of runtimeFileCache.keys()) {
      if (prefixes.some((prefix) => key === prefix || key.startsWith(`${prefix}/`)))
        affected.push(key);
    }
    // 保留中の読み取り・先読みも無効化する。名前変更後に旧入力元
    // ・上書き先を保持しない一方、一時ファイルの整理や不可分な
    // 保存でも、無関係なスキン・シナリオの Blob は保つ。
    invalidateRuntimeFiles(affected);
  };

  const cacheRuntimeBlob = (key, blob) => {
    if (!(blob instanceof Blob) || blob.size > RUNTIME_FILE_CACHE_MAX_FILE_BYTES) return;
    const previous = runtimeFileCache.get(key);
    if (previous && previous.blob) runtimeFileCacheBytes -= previous.blob.size;
    runtimeFileCache.set(key, {blob, promise: Promise.resolve(blob)});
    runtimeFileCacheBytes += blob.size;
    while (runtimeFileCacheBytes > RUNTIME_FILE_CACHE_MAX_BYTES && runtimeFileCache.size) {
      const oldestKey = runtimeFileCache.keys().next().value;
      const oldest = runtimeFileCache.get(oldestKey);
      runtimeFileCache.delete(oldestKey);
      if (oldest && oldest.blob) runtimeFileCacheBytes -= oldest.blob.size;
    }
  };

  const cacheRuntimePromise = (key, promise, generation = runtimeFileCacheGeneration) => {
    runtimeFileCache.set(key, {promise});
    promise.then((blob) => {
      const current = runtimeFileCache.get(key);
      if (current && current.promise === promise) {
        runtimeFileCache.delete(key);
        if (generation === runtimeFileCacheGeneration) cacheRuntimeBlob(key, blob);
      }
    }, () => {
      const current = runtimeFileCache.get(key);
      if (current && current.promise === promise) runtimeFileCache.delete(key);
    });
    return promise;
  };

  // 一つの C++ 操作が小さな複数ファイルを連続して
  // 開く場合がある。同じ保存領域の読み取りだけなのにパ
  // スごとにトランザクションを作ると、各ファイルでブラ
  // ウザーの処理と確定が必要になる。現在のマイクロタ
  // スク中の要求を一つにまとめる。キーごとの Promise
  // は runtimeFileCache に残
  // し、処理中の重複した open もまとめる。
  const flushRuntimeFileBatch = () => {
    runtimeFileBatchScheduled = false;
    if (!runtimeFileBatch.size) return;
    const entries = Array.from(runtimeFileBatch.values()).slice(0, RUNTIME_FILE_BATCH_MAX_FILES);
    for (const entry of entries) runtimeFileBatch.delete(entry.key);
    if (runtimeFileBatch.size) {
      runtimeFileBatchScheduled = true;
      queueMicrotask(flushRuntimeFileBatch);
    }

    const rejectAll = (error) => {
      for (const entry of entries) {
        if (entry.settled) continue;
        entry.settled = true;
        entry.reject(error);
      }
    };
    Promise.resolve(openStorageDb()).then((database) => {
      let transaction;
      try {
        transaction = database.transaction(FILE_STORE, "readonly");
        const store = transaction.objectStore(FILE_STORE);
        transaction.onerror = () => rejectAll(
          transaction.error || new Error("IndexedDB file batch failed"));
        transaction.onabort = () => rejectAll(
          transaction.error || new Error("IndexedDB file batch aborted"));
        for (const entry of entries) {
          let request;
          try {
            request = store.get(entry.key);
          } catch (error) {
            entry.settled = true;
            entry.reject(error);
            continue;
          }
          request.onsuccess = () => {
            if (entry.settled) return;
            entry.settled = true;
            entry.resolve(recordBlob(request.result));
          };
          request.onerror = () => {
            if (entry.settled) return;
            entry.settled = true;
            entry.reject(request.error || new Error("IndexedDB file request failed"));
          };
        }
      } catch (error) {
        rejectAll(error);
      }
    }, rejectAll);
  };

  const queueRuntimeFileRead = (key) => {
    const pending = runtimeFileBatch.get(key);
    if (pending) return pending.promise;
    let resolve;
    let reject;
    const promise = new Promise((resolveValue, rejectValue) => {
      resolve = resolveValue;
      reject = rejectValue;
    });
    const entry = {key, promise, resolve, reject, settled: false};
    runtimeFileBatch.set(key, entry);
    cacheRuntimePromise(key, promise);
    if (!runtimeFileBatchScheduled) {
      runtimeFileBatchScheduled = true;
      queueMicrotask(flushRuntimeFileBatch);
    }
    return promise;
  };

  const deleteDatabase = (name) => new Promise((resolve, reject) => {
    const request = indexedDB.deleteDatabase(name);
    request.onsuccess = () => resolve();
    request.onerror = () => reject(request.error || new Error(`${name}の削除に失敗しました。`));
    request.onblocked = () => reject(new Error(
      `${name}を使用中のタブがあります。ほかのCardWirthPy_C Webタブを閉じて再実行してください。`));
  });

  const closeDatabase = async (databasePromise) => {
    if (!databasePromise) return;
    try {
      const database = await databasePromise;
      if (database) database.close();
    } catch (_) {
      // 開く処理が失敗しても、deleteDatabase によるその名前の整理は妨げない。
    }
  };

  const deleteAllIndexedDb = async () => {
    await closeDatabase(storageDbPromise);
    storageDbPromise = undefined;
    initializationPromise = undefined;
    resolvedStorageMode = undefined;
    resolvedFileSystemDirectory = undefined;
    invalidateRuntimeStats();
    clearRuntimeFileCache();
    await Promise.all([
      deleteDatabase(STORAGE_DB_NAME),
      ...LEGACY_DATABASES.map(({name}) => deleteDatabase(name))
    ]);
  };

  const requestResult = (request) => new Promise((resolve, reject) => {
    request.onsuccess = () => resolve(request.result);
    request.onerror = () => reject(request.error || new Error("IndexedDB request failed"));
  });

  const transactionDone = (transaction) => new Promise((resolve, reject) => {
    transaction.oncomplete = () => resolve();
    transaction.onerror = () => reject(transaction.error || new Error("IndexedDB transaction failed"));
    transaction.onabort = () => reject(transaction.error || new Error("IndexedDB transaction aborted"));
  });

  const openDatabase = (name, version, upgrade) => new Promise((resolve, reject) => {
    const request = indexedDB.open(name, version);
    request.onupgradeneeded = (event) => upgrade(request.result, event.oldVersion, request.transaction);
    request.onsuccess = () => {
      const database = request.result;
      database.onversionchange = () => database.close();
      resolve(database);
    };
    request.onerror = () => reject(request.error || new Error(`Failed to open ${name}`));
    request.onblocked = () => reject(new Error(`${name} upgrade is blocked by another tab`));
  });

  // indexedDB.databases() 未対応のブラウザー
  // でも、新しい空の DB を残さずに旧 DB の存在を調べる。
  const openExistingDatabase = async (name) => {
    if (typeof indexedDB.databases === "function") {
      try {
        const databases = await indexedDB.databases();
        if (!databases.some((entry) => entry && entry.name === name)) return null;
      } catch (_) {
        // 列挙しない確認処理へ進む。
      }
    }
    return new Promise((resolve, reject) => {
      let created = false;
      const request = indexedDB.open(name);
      request.onupgradeneeded = (event) => {
        if (event.oldVersion === 0) {
          created = true;
          request.transaction.abort();
        }
      };
      request.onsuccess = () => {
        if (created) {
          request.result.close();
          resolve(null);
        } else {
          request.result.onversionchange = () => request.result.close();
          resolve(request.result);
        }
      };
      request.onerror = (event) => {
        if (created && request.error && request.error.name === "AbortError") {
          event.preventDefault();
          resolve(null);
        } else {
          reject(request.error || new Error(`Failed to open ${name}`));
        }
      };
    });
  };

  const readLegacyRecords = async ({name, store}) => {
    const database = await openExistingDatabase(name);
    if (!database) return [];
    try {
      if (!database.objectStoreNames.contains(store)) return [];
      const transaction = database.transaction(store, "readonly");
      return await requestResult(transaction.objectStore(store).getAll());
    } finally {
      database.close();
    }
  };

  const openStorageDb = () => {
    if (!storageDbPromise) {
      storageDbPromise = openDatabase(STORAGE_DB_NAME, STORAGE_DB_VERSION, (database) => {
        if (!database.objectStoreNames.contains(FILE_STORE)) {
          const store = database.createObjectStore(FILE_STORE, {keyPath: "key"});
          store.createIndex("scope", "scope", {unique: false});
          store.createIndex("parentKey", "parentKey", {unique: false});
          store.createIndex("pathKey", "pathKey", {unique: false});
        }
        if (!database.objectStoreNames.contains(STAT_STORE)) {
          const store = database.createObjectStore(STAT_STORE, {keyPath: "key"});
          store.createIndex("scope", "scope", {unique: false});
          store.createIndex("parentKey", "parentKey", {unique: false});
          store.createIndex("pathKey", "pathKey", {unique: false});
          store.createIndex("kind", "kind", {unique: false});
        }
        if (!database.objectStoreNames.contains(SETTINGS_STORE)) {
          database.createObjectStore(SETTINGS_STORE, {keyPath: "key"});
        }
      });
    }
    return storageDbPromise;
  };
  const openFileDb = openStorageDb;
  const openStatDb = openStorageDb;
  const openSettingsDb = openStorageDb;

  // 統一前は Blob・stat・設定を三つの DB に分け
  // ていた。新 DB にないキーだけを複製し、旧 DB は巻
  // き戻し用に保持する。全取り込み行と移行済みの印を一つの
  // トランザクションで確定し、既存のブラウザー UserData
  // を保ちつつ、古い行で新しい保存を上書きしない。
  const migrateLegacyDatabases = async () => {
    const database = await openStorageDb();
    const markerTransaction = database.transaction(SETTINGS_STORE, "readonly");
    const marker = await requestResult(
      markerTransaction.objectStore(SETTINGS_STORE).get(LEGACY_MIGRATION_KEY));
    if (marker) return marker.value || marker;

    const legacyRows = await Promise.all(LEGACY_DATABASES.map(readLegacyRecords));
    const transaction = database.transaction(
      [FILE_STORE, STAT_STORE, SETTINGS_STORE], "readwrite");
    const result = {completedAt: Date.now(), imported: 0};
    const candidates = [];
    LEGACY_DATABASES.forEach(({store}, index) => {
      for (const record of legacyRows[index]) {
        if (!record || record.key === undefined ||
            (store === SETTINGS_STORE && record.key === LEGACY_MIGRATION_KEY)) continue;
        candidates.push({store, record});
      }
    });
    let remaining = candidates.length;
    const writeMarker = () => transaction.objectStore(SETTINGS_STORE).put({
      key: LEGACY_MIGRATION_KEY, value: result, updatedAt: result.completedAt
    });
    if (remaining === 0) writeMarker();
    for (const {store, record} of candidates) {
      const destination = transaction.objectStore(store);
      // 存在だけを確認する。旧 get() と異なり、getKey() は
      // 移行時の上書き可否を決めるために保存済み Blob を複製しない。
      const request = destination.getKey(record.key);
      request.onsuccess = () => {
        if (request.result === undefined) {
          destination.put(record);
          result.imported++;
        }
        if (--remaining === 0) writeMarker();
      };
    }
    await transactionDone(transaction);
    return result;
  };

  const normalizePath = (value, directory = false) => {
    let path = String(value || "").normalize("NFC").replace(/\\/g, "/");
    path = path.replace(/\/{2,}/g, "/");
    if (!path.startsWith("/")) path = `/${path}`;
    const parts = [];
    for (const part of path.split("/")) {
      if (!part || part === ".") continue;
      if (part === ".." || part.includes("\0")) throw new Error(`Unsafe path: ${value}`);
      parts.push(part);
    }
    path = `/${parts.join("/")}`;
    if (directory && path !== "/") path += "/";
    return path;
  };

  const lowerPath = (value, directory = false) => normalizePath(value, directory).toLowerCase();
  const storageKey = (scope, path, directory = false) => `${scope}:${lowerPath(path, directory)}`;
  const parentPath = (value) => {
    const path = normalizePath(value).replace(/\/$/, "");
    const slash = path.lastIndexOf("/");
    return slash <= 0 ? "/" : `${path.slice(0, slash)}/`;
  };
  const basename = (value) => normalizePath(value).replace(/\/$/, "").split("/").pop() || "";
  const isTransientPath = (value) => TRANSIENT_NAMES.has(basename(value));

  const formatBytes = (value) => {
    const bytes = Math.max(0, Number(value) || 0);
    if (bytes < 1024) return `${bytes} B`;
    if (bytes < 1024 * 1024) return `${Math.ceil(bytes / 1024)} KiB`;
    return `${Math.ceil(bytes / (1024 * 1024))} MiB`;
  };

  const assertUniqueImportPaths = (entries) => {
    const seen = new Map();
    for (const entry of entries) {
      const path = normalizePath(entry.path);
      if (Array.from(path).length > IMPORT_LIMITS.maxPathCharacters) {
        throw new Error(
          `取込みパスが長すぎます（上限 ${IMPORT_LIMITS.maxPathCharacters}文字）: ${entry.sourcePath || path}`);
      }
      const key = lowerPath(path);
      const previous = seen.get(key);
      if (previous) {
        throw new Error(
          "大文字・小文字またはUnicode正規化後に同じパスとなるファイルがあります: " +
          `${previous.sourcePath || previous.path} / ${entry.sourcePath || path}`);
      }
      seen.set(key, entry);
    }
  };

  const assertImportSizes = (entries) => {
    if (entries.length > IMPORT_LIMITS.maxEntries) {
      throw new Error(
        `取込みファイル数が上限 ${IMPORT_LIMITS.maxEntries}件を超えています: ${entries.length}件`);
    }
    let totalBytes = 0;
    for (const entry of entries) {
      const size = Number(entry.uncompressedSize);
      if (!Number.isFinite(size) || size < 0) continue;
      if (size > IMPORT_LIMITS.maxFileBytes) {
        throw new Error(
          `単一ファイルの展開サイズが上限 ${formatBytes(IMPORT_LIMITS.maxFileBytes)}を超えています: ` +
          `${entry.sourcePath || entry.path} (${formatBytes(size)})`);
      }
      totalBytes += size;
      if (totalBytes > IMPORT_LIMITS.maxTotalBytes) {
        throw new Error(
          `総展開サイズが上限 ${formatBytes(IMPORT_LIMITS.maxTotalBytes)}を超えています。`);
      }
      const compressedSize = Number(entry.compressedSize);
      if (size > 1024 * 1024 && Number.isFinite(compressedSize) && compressedSize >= 0 &&
          size / Math.max(1, compressedSize) > IMPORT_LIMITS.maxCompressionRatio) {
        throw new Error(
          `ZIP圧縮率が安全上限 ${IMPORT_LIMITS.maxCompressionRatio}倍を超えています: ` +
          `${entry.sourcePath || entry.path}`);
      }
    }
    return totalBytes;
  };

  const ensureStorageCapacity = async (requiredBytes) => {
    const storage = global.navigator && global.navigator.storage;
    if (!storage || typeof storage.estimate !== "function" || requiredBytes <= 0) return;
    let estimate;
    try { estimate = await storage.estimate(); }
    catch (_) { return; }
    const quota = Number(estimate && estimate.quota);
    const usage = Number(estimate && estimate.usage);
    if (!Number.isFinite(quota) || !Number.isFinite(usage)) return;
    const available = Math.max(0, quota - usage - IMPORT_LIMITS.quotaReserveBytes);
    if (requiredBytes > available) {
      throw new Error(
        `ブラウザ保存領域が不足しています。必要: ${formatBytes(requiredBytes)} ` +
        `空き目安: ${formatBytes(available)}`);
    }
  };

  const directoryStats = (scope, value, timestamp = Date.now()) => {
    const path = normalizePath(value);
    const parts = path.split("/").filter(Boolean);
    const stats = [];
    let current = "/";
    stats.push({
      key: storageKey(scope, "/", true), scope, path: "/", pathKey: "/",
      parentKey: null, kind: "directory", size: 0, mtimeMs: timestamp,
      updatedAt: timestamp
    });
    for (let index = 0; index < Math.max(0, parts.length - 1); index++) {
      current += `${parts[index]}/`;
      stats.push({
        key: storageKey(scope, current, true), scope, path: current,
        pathKey: lowerPath(current, true), parentKey: storageKey(scope, parentPath(current), true),
        kind: "directory", size: 0, mtimeMs: timestamp, updatedAt: timestamp
      });
    }
    return stats;
  };

  const getRecord = async (databasePromise, storeName, key) => {
    const database = await databasePromise;
    const transaction = database.transaction(storeName, "readonly");
    return requestResult(transaction.objectStore(storeName).get(key));
  };

  const putRecords = async (databasePromise, storeName, records) => {
    if (!records.length) return;
    const database = await databasePromise;
    const transaction = database.transaction(storeName, "readwrite");
    const store = transaction.objectStore(storeName);
    for (const record of records) store.put(record);
    await transactionDone(transaction);
    if (storeName === STAT_STORE) {
      for (const scope of new Set(records.map((record) => record && record.scope))) {
        if (scope) invalidateRuntimeStats(scope);
      }
      invalidateRuntimeFiles(records.filter((record) => record.kind === "file").map((record) => record.key));
    }
  };

  // Blob の内容と stat の行は別の保存領域に置き、内容
  // を読まずに一覧・stat を取得できるようにする。ただし両方
  // を変える場合は、共有 DB の一つのトランザクションで書く。
  const putFileAndStatRecords = async (fileRecords, statRecords) => {
    if (!fileRecords.length && !statRecords.length) return;
    // 取り込み後に Blob を読み戻すのではなく、入力内容を呼び出し側がす
    // でに持つ書き込み前の段階で、不可分な保存に先立ってサイズを検証する。
    const fileStats = new Map(statRecords.filter(record => record.kind === "file")
      .map(record => [record.key, record]));
    for (const record of fileRecords) {
      const stat = fileStats.get(record.key);
      const size = Number(record.data?.size ?? record.data?.byteLength ?? -1);
      if (!stat || stat.scope !== record.scope || stat.path !== record.path ||
          !Number.isSafeInteger(size) || size < 0 || size !== Number(stat.size))
        throw new Error(`ファイル本体とstatが一致しません: ${record.path}`);
    }
    const database = await openStorageDb();
    const transaction = database.transaction([FILE_STORE, STAT_STORE], "readwrite");
    const fileStore = transaction.objectStore(FILE_STORE);
    const statStore = transaction.objectStore(STAT_STORE);
    for (const record of fileRecords) fileStore.put(record);
    for (const record of statRecords) statStore.put(record);
    await transactionDone(transaction);
    for (const scope of new Set(statRecords.map((record) => record && record.scope))) {
      if (scope) invalidateRuntimeStats(scope);
    }
    invalidateRuntimeFiles(fileRecords.map((record) => record.key));
    for (const record of fileRecords) cacheRuntimeBlob(record.key, record.data);
  };

  const recordsForScope = async (databasePromise, storeName, scope) => {
    const database = await databasePromise;
    const transaction = database.transaction(storeName, "readonly");
    const store = transaction.objectStore(storeName);
    return requestResult(store.index("scope").getAll(IDBKeyRange.only(scope)));
  };

  const countScope = async (databasePromise, storeName, scope) => {
    const database = await databasePromise;
    const transaction = database.transaction(storeName, "readonly");
    const store = transaction.objectStore(storeName);
    return requestResult(store.index("scope").count(IDBKeyRange.only(scope)));
  };

  const verifyFileScope = async (scope, expectedCount) => {
    // 意図的な差。存在確認は主キーだけを取得し、Blob レコー
    // ドは読まない。サイズは共通の不可分な書き込み前に検証する。
    const database = await openStorageDb();
    const transaction = database.transaction([FILE_STORE, STAT_STORE], "readonly");
    const range = IDBKeyRange.only(scope);
    const [files, stats] = await Promise.all([
      requestResult(transaction.objectStore(FILE_STORE).index("scope").getAllKeys(range)),
      requestResult(transaction.objectStore(STAT_STORE).index("scope").getAll(range))
    ]);
    const fileStats = new Map(stats.filter((record) => record.kind === "file")
      .map((record) => [record.key, record]));
    if (files.length !== expectedCount || fileStats.size !== expectedCount)
      return {ok: false, files: files.length, stats: fileStats.size};
    for (const key of files) {
      const stat = fileStats.get(key);
      const size = Number(stat?.size);
      if (!stat || !Number.isSafeInteger(size) || size < 0)
        return {ok: false, files: files.length, stats: fileStats.size};
    }
    return {ok: true, files: files.length, stats: fileStats.size};
  };

  const deleteScope = async (databasePromise, storeName, scope) => {
    const database = await databasePromise;
    const transaction = database.transaction(storeName, "readwrite");
    const index = transaction.objectStore(storeName).index("scope");
    const request = index.openKeyCursor(IDBKeyRange.only(scope));
    request.onsuccess = () => {
      const cursor = request.result;
      if (!cursor) return;
      transaction.objectStore(storeName).delete(cursor.primaryKey);
      cursor.continue();
    };
    await transactionDone(transaction);
    if (storeName === STAT_STORE) {
      invalidateRuntimeStats(scope);
      clearRuntimeFileCache();
    }
  };

  const deleteScopeFromStore = (transaction, storeName, scope) => {
    const store = transaction.objectStore(storeName);
    const request = store.index("scope").openKeyCursor(IDBKeyRange.only(scope));
    request.onsuccess = () => {
      const cursor = request.result;
      if (!cursor) return;
      store.delete(cursor.primaryKey);
      cursor.continue();
    };
  };

  const deleteFileAndStatScope = async (scope) => {
    const database = await openStorageDb();
    const transaction = database.transaction([FILE_STORE, STAT_STORE], "readwrite");
    deleteScopeFromStore(transaction, FILE_STORE, scope);
    deleteScopeFromStore(transaction, STAT_STORE, scope);
    await transactionDone(transaction);
    invalidateRuntimeStats(scope);
    clearRuntimeFileCache();
  };

  const setSetting = async (key, value) => {
    const database = await openSettingsDb();
    const transaction = database.transaction(SETTINGS_STORE, "readwrite");
    transaction.objectStore(SETTINGS_STORE).put({key, value, updatedAt: Date.now()});
    await transactionDone(transaction);
  };

  const getSetting = async (key) => {
    const record = await getRecord(openSettingsDb(), SETTINGS_STORE, key);
    return record ? record.value : null;
  };

  const relativeUploadPath = (file) => {
    let value = String(file.webkitRelativePath || file.name || "").normalize("NFC").replace(/\\/g, "/");
    const parts = value.split("/").filter(Boolean);
    if (file.webkitRelativePath && parts.length > 1) parts.shift();
    if (!parts.length || parts.includes("..")) throw new Error(`Invalid upload path: ${value}`);
    return normalizePath(`/user/${parts.join("/")}`);
  };

  const queueUserFolder = async (fileList, progress) => {
    const input = Array.from(fileList || []);
    const entries = [];
    for (const file of input) {
      const path = relativeUploadPath(file);
      if (isTransientPath(path)) continue;
      entries.push({
        file,
        path,
        sourcePath: String(file.webkitRelativePath || file.name || path),
        uncompressedSize: Number(file.size || 0)
      });
    }
    if (!entries.length) throw new Error("UserDataフォルダーにファイルがありません。");
    assertUniqueImportPaths(entries);
    const totalBytes = assertImportSizes(entries);
    // 新しい準備世代を始める前に、確定済みの前の世代の処理を完了する。Blob/stat
    // の本番への移行は、共有 DB 内で不可分に行う。
    await applyQueuedUserFolder();
    await setSetting(UPLOAD_READY_KEY, null);
    await deleteFileAndStatScope("upload");
    let promotionStarted = false;
    try {
      await ensureStorageCapacity(totalBytes);
      const allDirectoryStats = new Map();
      for (let offset = 0; offset < entries.length; offset += 256) {
        const batch = entries.slice(offset, offset + 256);
        const now = Date.now();
        const fileRecords = [];
        const statRecords = [];
        for (const {file, path} of batch) {
          const key = storageKey("upload", path);
          const parentKey = storageKey("upload", parentPath(path), true);
          fileRecords.push({
            key, scope: "upload", path, pathKey: lowerPath(path), parentKey,
            data: file, updatedAt: now
          });
          statRecords.push({
            key, scope: "upload", path, pathKey: lowerPath(path), parentKey, kind: "file",
            size: file.size, mtimeMs: Number(file.lastModified || now), updatedAt: now
          });
          for (const stat of directoryStats("upload", path, now)) allDirectoryStats.set(stat.key, stat);
        }
        await putFileAndStatRecords(fileRecords, statRecords);
        if (progress) progress(Math.min(offset + batch.length, entries.length), entries.length);
      }
      await putRecords(openStatDb(), STAT_STORE, Array.from(allDirectoryStats.values()));
      const staged = await verifyFileScope("upload", entries.length);
      if (!staged.ok) {
        throw new Error(
          `UserDataフォルダーのstaging確認に失敗しました。` +
          ` 入力=${entries.length} 本体=${staged.files} 一覧=${staged.stats}`);
      }
      await setSetting(UPLOAD_READY_KEY, {
        expectedFiles: entries.length,
        readyAt: Date.now()
      });
      promotionStarted = true;
      await applyQueuedUserFolder();
      return entries.length;
    } catch (error) {
      if (!promotionStarted) {
        await Promise.allSettled([deleteFileAndStatScope("upload")]);
      }
      throw error;
    }
  };

  const zipUploadPath = (entry) => {
    const original = String(entry.unsafeOriginalName || entry.name || "")
      .normalize("NFC").replace(/\\/g, "/");
    if (!original || original.startsWith("/") || /^[A-Za-z]:\//.test(original) ||
        original.includes("\0"))
      throw new Error(`ZIP内に不正なパスがあります: ${original}`);
    const parts = original.split("/").filter((part) => part && part !== ".");
    if (!parts.length || parts.includes(".."))
      throw new Error(`ZIP内に不正なパスがあります: ${original}`);
    if (parts[0].toLowerCase() === "userdata") parts.shift();
    if (!parts.length) return null;
    return normalizePath(`/user/${parts.join("/")}`);
  };

  // 一つの UserData.zip を準備領域へ項目ごとに取り込む。すべて
  // の展開・書き込みが終わった後だけ、既存 UserData を置き換える。
  const queueUserZip = async (zipFile, progress) => {
    if (!zipFile) throw new Error("UserData.zipが選択されていません。");
    if (!global.JSZip) throw new Error("ZIP展開ライブラリを読み込めませんでした。");
    const archiveBytes = Number(zipFile.size ?? zipFile.byteLength ?? 0);
    if (Number.isFinite(archiveBytes) && archiveBytes > IMPORT_LIMITS.maxArchiveBytes) {
      throw new Error(
        `ZIPファイルが上限 ${formatBytes(IMPORT_LIMITS.maxArchiveBytes)}を超えています。`);
    }
    const zip = await global.JSZip.loadAsync(zipFile, {
      createFolders: false,
      decodeFileName: (bytes) => {
        // Engine/Python のアーカイブ境界に合わせ、有効な
        // UTF-8、次に日本語の旧符号化を優先する。復号エラーを
        // 致命的に扱い、置換文字で保存パスが黙って変わることを防ぐ。
        for (const encoding of ["utf-8", "shift-jis", "euc-jp"]) {
          try { return new TextDecoder(encoding, {fatal: true}).decode(bytes); }
          catch (_) { }
        }
        return new TextDecoder("utf-8").decode(bytes);
      }
    });
    const entries = [];
    let fileEntryCount = 0;
    for (const entry of Object.values(zip.files)) {
      if (!entry || entry.dir) continue;
      fileEntryCount++;
      if (fileEntryCount > IMPORT_LIMITS.maxEntries) {
        throw new Error(
          `ZIP内のファイル数が上限 ${IMPORT_LIMITS.maxEntries}件を超えています。`);
      }
      const path = zipUploadPath(entry);
      if (!path || isTransientPath(path)) continue;
      const metadata = entry._data || {};
      entries.push({
        entry,
        path,
        sourcePath: String(entry.unsafeOriginalName || entry.name || path),
        compressedSize: Number(metadata.compressedSize),
        uncompressedSize: Number(metadata.uncompressedSize)
      });
    }
    if (!entries.length) throw new Error("UserData.zipにファイルがありません。");
    assertUniqueImportPaths(entries);
    const metadataTotalBytes = assertImportSizes(entries);

    await applyQueuedUserFolder();
    await setSetting(UPLOAD_READY_KEY, null);
    await deleteFileAndStatScope("upload");
    let promotionStarted = false;
    try {
      await ensureStorageCapacity(metadataTotalBytes);
      const directoryRecords = new Map();
      let fileRecords = [];
      let statRecords = [];
      let expandedBytes = 0;
      const flush = async () => {
        await putFileAndStatRecords(fileRecords, statRecords);
        fileRecords = [];
        statRecords = [];
      };
      for (let index = 0; index < entries.length; index++) {
        const {entry, path, sourcePath} = entries[index];
        const blob = await entry.async("blob", (metadata) => {
          if (progress) progress(index, entries.length, path, Number(metadata.percent || 0));
        });
        if (blob.size > IMPORT_LIMITS.maxFileBytes) {
          throw new Error(
            `単一ファイルの展開サイズが上限 ${formatBytes(IMPORT_LIMITS.maxFileBytes)}を超えています: ${sourcePath}`);
        }
        expandedBytes += blob.size;
        if (expandedBytes > IMPORT_LIMITS.maxTotalBytes) {
          throw new Error(
            `総展開サイズが上限 ${formatBytes(IMPORT_LIMITS.maxTotalBytes)}を超えています。`);
        }
        const now = Date.now();
        const key = storageKey("upload", path);
        const parentKey = storageKey("upload", parentPath(path), true);
        fileRecords.push({
          key, scope: "upload", path, pathKey: lowerPath(path), parentKey,
          data: blob, updatedAt: now
        });
        statRecords.push({
          key, scope: "upload", path, pathKey: lowerPath(path), parentKey,
          kind: "file", size: blob.size,
          mtimeMs: entry.date instanceof Date ? entry.date.getTime() : now,
          updatedAt: now
        });
        for (const stat of directoryStats("upload", path, now))
          directoryRecords.set(stat.key, stat);
        if (fileRecords.length >= 32) await flush();
        if (progress) progress(index + 1, entries.length, path, 100);
      }
      await flush();
      await putRecords(openStatDb(), STAT_STORE, Array.from(directoryRecords.values()));
      const staged = await verifyFileScope("upload", entries.length);
      if (!staged.ok) {
        throw new Error(
          `UserData.zipのstaging確認に失敗しました。` +
          ` ZIP=${entries.length} 本体=${staged.files} 一覧=${staged.stats}`);
      }
      await setSetting(UPLOAD_READY_KEY, {
        expectedFiles: entries.length,
        readyAt: Date.now()
      });
      promotionStarted = true;
      await applyQueuedUserFolder();

      // JSZip が完了しただけで成功を報告しない。本番への
      // 移行後に、内容の保存領域と別の stat 領域の行を両
      // 方確認する。この検査では Blob の内容は読まない。
      const stored = await verifyFileScope("user", entries.length);
      if (!stored.ok) {
        throw new Error(
          `UserData.zipの保存確認に失敗しました。` +
          ` ZIP=${entries.length} 本体=${stored.files} 一覧=${stored.stats}`);
      }
      return entries.length;
    } catch (error) {
      if (!promotionStarted) {
        await Promise.allSettled([deleteFileAndStatScope("upload")]);
      }
      throw error;
    }
  };

  // 準備レコードの移行は IndexedDB のキー変更だけで行う。ここ
  // で File/Blob を ArrayBuffer に変換せず、数
  // GiB の UserData 全体を取り込み中にメモリーへ読まない。
  const promoteStoreScope = (transaction, storeName, source, target) => {
    const store = transaction.objectStore(storeName);
    const deleteRequest = store.index("scope").openKeyCursor(IDBKeyRange.only(target));
    deleteRequest.onsuccess = () => {
      const deleteCursor = deleteRequest.result;
      if (deleteCursor) {
        store.delete(deleteCursor.primaryKey);
        deleteCursor.continue();
        return;
      }

      const moveRequest = store.index("scope").openCursor(IDBKeyRange.only(source));
      moveRequest.onsuccess = () => {
        const cursor = moveRequest.result;
        if (!cursor) return;
        const record = cursor.value;
        const directory = record.kind === "directory" || String(record.path || "").endsWith("/");
        record.scope = target;
        record.key = storageKey(target, record.path, directory);
        record.pathKey = lowerPath(record.path, directory);
        record.parentKey = record.path === "/" ? null : storageKey(target, parentPath(record.path), true);
        store.put(record);
        cursor.delete();
        cursor.continue();
      };
    };
  };

  const promoteScope = async (source, target) => {
    const database = await openStorageDb();
    const transaction = database.transaction(
      [FILE_STORE, STAT_STORE, SETTINGS_STORE], "readwrite");
    promoteStoreScope(transaction, FILE_STORE, source, target);
    promoteStoreScope(transaction, STAT_STORE, source, target);
    // ready の印は同じ世代を表す。このトランザクション内で消去
    // し、新しい Blob/stat の組と確定状態を不可分にする。
    transaction.objectStore(SETTINGS_STORE).delete(UPLOAD_READY_KEY);
    await transactionDone(transaction);
    invalidateRuntimeStats(source);
    invalidateRuntimeStats(target);
    clearRuntimeFileCache();
  };

  const applyQueuedUserFolder = async () => {
    const ready = await getSetting(UPLOAD_READY_KEY);
    // 通常起動にはアップロードの印がない。その確認だけで UserData
    // 全体を列挙せず、二つの索引付き件数で、ブラウザー終了
    // により残った不完全なアップロードを検出・整理する。有効
    // な印で移行待ちと確認できた場合だけ全レコードを読む。
    const [uploadFiles, uploadStatCount] = await Promise.all([
      countScope(openFileDb(), FILE_STORE, "upload"),
      countScope(openStatDb(), STAT_STORE, "upload")
    ]);
    const expected = Number(ready && ready.expectedFiles);

    if (!Number.isInteger(expected) || expected <= 0) {
      // ブラウザー終了で途切れた展開には完了の印がないた
      // め、UserData に上書きしてはならない。
      if (uploadFiles || uploadStatCount) {
        await deleteFileAndStatScope("upload");
      }
      if (ready) await setSetting(UPLOAD_READY_KEY, null);
      return 0;
    }

    const uploadStatRecords = uploadStatCount
      ? await recordsForScope(openStatDb(), STAT_STORE, "upload") : [];
    const uploadFileStats = uploadStatRecords.filter((record) => record.kind === "file").length;
    // 利用者レコードが必要なのは、対応するアップロー
    // ド領域がすでに空、つまりタブ終了前に移行が完了
    // した場合だけ。通常のアップロード待ちで無関係な
    // UserData レコードを読み込まない。
    const [userFiles, userStatRecords] = await Promise.all([
      uploadFiles === 0 ? countScope(openFileDb(), FILE_STORE, "user") : Promise.resolve(0),
      uploadFileStats === 0 ? recordsForScope(openStatDb(), STAT_STORE, "user") : Promise.resolve([])
    ]);
    const userFileStats = userStatRecords.filter((record) => record.kind === "file").length;

    const fileNeedsPromotion = uploadFiles === expected;
    const fileWasPromoted = uploadFiles === 0 && userFiles === expected;
    const statNeedsPromotion = uploadFileStats === expected;
    const statWasPromoted = uploadFileStats === 0 && userFileStats === expected;
    if ((!fileNeedsPromotion && !fileWasPromoted) ||
        (!statNeedsPromotion && !statWasPromoted)) {
      throw new Error(
        `UserData stagingの復旧判定に失敗しました。期待=${expected} ` +
        `本体(upload=${uploadFiles}, user=${userFiles}) ` +
        `一覧(upload=${uploadFileStats}, user=${userFileStats})`);
    }

    // 両保存領域を同じ世代にそろえる。置換と印の
    // 消去は一つのトランザクションで確定する。
    if (fileNeedsPromotion && statNeedsPromotion) {
      await promoteScope("upload", "user");
    } else if (!(fileWasPromoted && statWasPromoted)) {
      throw new Error("Blobとstatのstaging世代が一致しません。");
    }

    const [committedFiles, committedStats] = await Promise.all([
      countScope(openFileDb(), FILE_STORE, "user"),
      recordsForScope(openStatDb(), STAT_STORE, "user")
    ]);
    const committedFileStats = committedStats.filter((record) => record.kind === "file").length;
    if (committedFiles !== expected || committedFileStats !== expected) {
      throw new Error(
        `UserData stagingのcommit確認に失敗しました。期待=${expected} ` +
        `本体=${committedFiles} 一覧=${committedFileStats}`);
    }
    if (fileWasPromoted && statWasPromoted)
      await setSetting(UPLOAD_READY_KEY, null);
    return expected;
  };

  const supportsFileSystemAccess = () =>
    typeof global.showDirectoryPicker === "function" && !!global.isSecureContext;

  const getStorageMode = async () => {
    if (resolvedStorageMode !== undefined) return resolvedStorageMode;
    const mode = await getSetting(MODE_KEY);
    resolvedStorageMode = mode === "filesystem" && supportsFileSystemAccess()
      ? "filesystem" : "indexeddb";
    return resolvedStorageMode;
  };

  let fileSystemIndexEpoch = 0;
  const setStorageMode = async (mode) => {
    ++fileSystemIndexEpoch;
    const value = mode === "filesystem" && supportsFileSystemAccess() ? "filesystem" : "indexeddb";
    await setSetting(MODE_KEY, value);
    resolvedStorageMode = value;
    clearRuntimeFileCache();
    return value;
  };

  const chooseFileSystemDirectory = async () => {
    if (!supportsFileSystemAccess()) throw new Error("File System Access API is not available");
    const handle = await global.showDirectoryPicker({mode: "readwrite", startIn: "documents"});
    ++fileSystemIndexEpoch;
    await setSetting(DIRECTORY_HANDLE_KEY, handle);
    resolvedFileSystemDirectory = handle;
    await setStorageMode("filesystem");
    return handle;
  };

  const getFileSystemDirectory = async () => {
    if (resolvedFileSystemDirectory !== undefined) return resolvedFileSystemDirectory;
    resolvedFileSystemDirectory = await getSetting(DIRECTORY_HANDLE_KEY);
    return resolvedFileSystemDirectory;
  };

  const hasHandlePermission = async (handle, request = false) => {
    if (!handle) return false;
    const options = {mode: "readwrite"};
    if (await handle.queryPermission(options) === "granted") return true;
    if (!request) return false;
    return (await handle.requestPermission(options)) === "granted";
  };

  const walkFileSystemDirectory = async (rootHandle) => {
    const files = [];
    let visitedEntries = 0;
    const walk = async (directory, prefix) => {
      for await (const [name, handle] of directory.entries()) {
        if (TRANSIENT_NAMES.has(name)) continue;
        const relative = prefix ? `${prefix}/${name}` : name;
        visitedEntries++;
        if (visitedEntries > IMPORT_LIMITS.maxEntries) {
          throw new Error(
            `フォルダー内の項目数が多すぎます（上限 ${IMPORT_LIMITS.maxEntries}件）。`);
        }
        if (Array.from(relative.normalize("NFC")).length > IMPORT_LIMITS.maxPathCharacters) {
          throw new Error(
            `フォルダー内のパスが長すぎます（上限 ${IMPORT_LIMITS.maxPathCharacters}文字）: ${relative}`);
        }
        if (handle.kind === "directory") {
          files.push({relative, handle}); // 空ディレクトリーも永続的な項目として保存する。
          await walk(handle, relative);
        }
        else if (handle.kind === "file") files.push({relative, handle});
      }
    };
    await walk(rootHandle, "");
    return files;
  };

  const importFileSystemDirectory = async (_writeFile, _exists, progress) => {
    const epoch = ++fileSystemIndexEpoch;
    if (await getStorageMode() !== "filesystem") return {count: 0, permission: true};
    const rootHandle = await getFileSystemDirectory();
    if (!rootHandle || !(await hasHandlePermission(rootHandle, false))) {
      return {count: 0, permission: false};
    }
    const files = (await walkFileSystemDirectory(rootHandle)).map((entry) => ({
      ...entry,
      path: normalizePath(`/user/${entry.relative}`, entry.handle.kind === "directory"),
      sourcePath: entry.relative
    }));
    assertUniqueImportPaths(files);
    // 現在の索引を置き換える前に新しい世代を作る。プロバイダー失敗時に
    // 、空ディレクトリーも含む以前のメタデータを失わないようにする。
    const generation = `filesystem-index-${Date.now()}-${Math.random().toString(16).slice(2)}`;
    try {
      const directoryRecords = new Map();
      const statBatch = [];
      for (let index = 0; index < files.length; index++) {
        const {handle, path} = files[index];
        // getFile() はサイズ・更新日時と Blob に基づくハンドルを返す。arrayBuffer()
        // は呼ばず、内容は後でそのパスを開いたときだけ読む。
        const directory = handle.kind === "directory";
        const file = directory ? null : await handle.getFile();
        const key = storageKey(generation, path, directory);
        const now = Date.now();
        for (const record of directoryStats(generation, path, now))
          directoryRecords.set(record.key, record);
        statBatch.push({
          key, scope: generation, path, pathKey: lowerPath(path, directory),
          parentKey: storageKey(generation, parentPath(path), true), kind: directory ? "directory" : "file",
          size: directory ? 0 : file.size, mtimeMs: directory ? now : file.lastModified, updatedAt: now
        });
        if (statBatch.length >= 256) {
          await putRecords(openStatDb(), STAT_STORE, statBatch.splice(0));
        }
        if (progress) progress(index + 1, files.length);
      }
      await putRecords(openStatDb(), STAT_STORE, statBatch);
      await putRecords(openStatDb(), STAT_STORE, Array.from(directoryRecords.values()));
      const database = await openStatDb();
      if (epoch !== fileSystemIndexEpoch) {
        throw new Error("接続フォルダーまたは再索引要求が変更されたため、古い索引を破棄しました。");
      }
      const transaction = database.transaction(STAT_STORE, "readwrite");
      promoteStoreScope(transaction, STAT_STORE, generation, "filesystem");
      await transactionDone(transaction);
      invalidateRuntimeStats("filesystem");
      clearRuntimeFileCache();
      return {count: files.filter(entry => entry.handle.kind === "file").length, permission: true};
    } finally {
      await deleteScope(openStatDb(), STAT_STORE, generation).catch(() => {});
    }
  };

  const resolveDirectory = async (root, parts) => {
    let directory = root;
    for (const part of parts) directory = await directory.getDirectoryHandle(part, {create: true});
    return directory;
  };

  const exportFileSystemDirectory = async (listFiles, progress) => {
    if (await getStorageMode() !== "filesystem") return {count: 0, permission: true};
    const rootHandle = await getFileSystemDirectory();
    if (!rootHandle || !(await hasHandlePermission(rootHandle, false))) {
      return {count: 0, permission: false};
    }
    const files = (await listFiles()).filter((entry) => !isTransientPath(entry.path));
    let written = 0;
    for (let index = 0; index < files.length; index++) {
      const entry = files[index];
      const path = normalizePath(entry.path);
      const key = storageKey("filesystem", path);
      const previous = await getRecord(openStatDb(), STAT_STORE, key);
      if (!previous || previous.fsSize !== entry.size || previous.fsMtimeMs !== entry.mtimeMs) {
        const relative = path.replace(/^\/user\//, "");
        const parts = relative.split("/").filter(Boolean);
        const name = parts.pop();
        const directory = await resolveDirectory(rootHandle, parts);
        const handle = await directory.getFileHandle(name, {create: true});
        const writable = await handle.createWritable();
        await writable.write(await entry.read());
        await writable.close();
        const diskFile = await handle.getFile();
        const now = Date.now();
        await putRecords(openStatDb(), STAT_STORE, [
          ...directoryStats("filesystem", path, now),
          {
            key, scope: "filesystem", path, pathKey: lowerPath(path),
            parentKey: storageKey("filesystem", parentPath(path), true), kind: "file",
            size: entry.size, mtimeMs: entry.mtimeMs, diskSize: diskFile.size,
            diskMtimeMs: diskFile.lastModified, fsSize: entry.size,
            fsMtimeMs: entry.mtimeMs, updatedAt: now
          }
        ]);
        written++;
      }
      if (progress) progress(index + 1, files.length);
    }
    return {count: written, permission: true};
  };

  const getStorageSummary = async () => {
    const [uploadStats, userStats, userPayloads] = await Promise.all([
      getRuntimeStatsForScope("upload"),
      getRuntimeStatsForScope("user"),
      countScope(openFileDb(), FILE_STORE, "user")
    ]);
    const userFiles = userStats.filter((record) => record.kind === "file").length;
    return {
      mode: await getStorageMode(),
      pendingUploads: uploadStats.filter((record) => record.kind === "file").length,
      userFiles,
      userPayloads,
      userDataComplete: userFiles === userPayloads,
      fileSystemSupported: supportsFileSystemAccess()
    };
  };

  const getRuntimeStatsForScope = async (scope) => {
    const cached = runtimeStatsCache.get(scope);
    if (cached) return cached;

    const userStats = await recordsForScope(openStatDb(), STAT_STORE, scope);
    const byPath = new Map();
    for (const record of userStats) {
      if (!record || !record.path) continue;
      // 行ごとに一度だけ正規化する。lowerPath と保
      // 存表記の両方で normalizePath を呼ぶと
      // 、大きな UserData ツリーで負荷が増える。
      const directory = record.kind === "directory";
      const path = normalizePath(record.path, directory);
      byPath.set(path.toLowerCase(), {
        path,
        kind: record.kind,
        size: Number(record.size || 0),
        mtimeMs: Number(record.mtimeMs ?? record.updatedAt ?? Date.now())
      });
    }
    // 不足する親ディレクトリーはローダーが必要時に作るため、IndexedDB
    // の列挙でロケールを考慮した深さ順のソートは不要。全パスの
    // ソートは検索・保存の動作を変えずに、大きな UserData
    // で O(n log n) の起動負荷を加えてしまう。
    const result = Array.from(byPath.values());
    runtimeStatsCache.set(scope, result);
    return result;
  };

  const getRuntimeStats = async () => {
    const mode = await getStorageMode();
    return getRuntimeStatsForScope(mode === "filesystem" ? "filesystem" : "user");
  };

  const recordBlob = (record) => {
    // Web の現行スキーマは、シーク可能な Python のディスク入力
    // と異なり、一レコードに全ファイルの Blob を保存する。IDB
    // の get 後に Blob.slice を呼んでも、ブラウザーが全
    // レコードを取得しない保証はない。この全体取得は Web ZIP 固
    // 有の制約として扱う。保存スキーマは保ち、共通 ZIP パーサーは
    // Blob から必要範囲だけを読む。IDB の部分取得とは区別する。
    if (!record || record.data == null) return null;
    return record.data instanceof Blob
      ? record.data
      : new Blob([record.data], {type: "application/octet-stream"});
  };

  const persistentAssetKey = (key) => {
    const value = String(key || "").normalize("NFC");
    if (!value || value.length > 256 || value.includes("\0"))
      throw new Error("永続キャッシュキーが不正です。");
    return `${PERSISTENT_ASSET_SCOPE}:${value}`;
  };

  const getPersistentAsset = async (key) => {
    const record = await getRecord(
      openFileDb(), FILE_STORE, persistentAssetKey(key));
    return recordBlob(record);
  };

  const savePersistentAsset = async (key, data) => {
    const storageKey = persistentAssetKey(key);
    if (data == null) throw new Error("永続キャッシュのデータがありません。");
    const blob = data instanceof Blob
      ? data
      : new Blob([data], {type: "application/octet-stream"});
    if (blob.size > PERSISTENT_ASSET_MAX_BYTES)
      throw new Error("永続キャッシュのサイズが上限を超えています。");
    const database = await openFileDb();
    const transaction = database.transaction(FILE_STORE, "readwrite");
    transaction.objectStore(FILE_STORE).put({
      key: storageKey,
      scope: PERSISTENT_ASSET_SCOPE,
      path: `/${storageKey}`,
      pathKey: storageKey.toLowerCase(),
      parentKey: null,
      data: blob,
      updatedAt: Date.now()
    });
    await transactionDone(transaction);
    return blob.size;
  };

  const prefetchRuntimeFiles = async (paths, {
    maxFiles = 2048,
    maxBytes = 16 * 1024 * 1024
  } = {}) => {
    if (await getStorageMode() === "filesystem") return 0;
    const keys = [];
    const seen = new Set();
    const sizeByPath = new Map((runtimeStatsCache.get("user") || [])
      .map((entry) => [entry.path.toLowerCase(), Number(entry.size || 0)]));
    let requestedBytes = 0;
    for (const value of paths || []) {
      const requestedPath = value && typeof value === "object" ? value.path : value;
      const path = normalizePath(requestedPath);
      if (!path.startsWith("/user/")) continue;
      const key = storageKey("user", path);
      if (seen.has(key) || runtimeFileCache.has(key)) continue;
      const declaredSize = value && typeof value === "object" ? value.size : undefined;
      const size = Number(declaredSize ?? sizeByPath.get(path.toLowerCase()) ?? 0);
      if (size < 0 || size > RUNTIME_FILE_CACHE_MAX_FILE_BYTES ||
          requestedBytes + size > maxBytes || keys.length >= maxFiles) continue;
      seen.add(key);
      keys.push(key);
      requestedBytes += size;
    }
    if (!keys.length) return 0;

    const database = await openStorageDb();
    const transaction = database.transaction(FILE_STORE, "readonly");
    const store = transaction.objectStore(FILE_STORE);
    let count = 0;
    const generation = runtimeFileCacheGeneration;
    for (const key of keys) {
      const request = store.get(key);
      request.onsuccess = () => {
        const blob = recordBlob(request.result);
        if (blob) {
          if (generation === runtimeFileCacheGeneration) cacheRuntimeBlob(key, blob);
          count++;
        }
      };
    }
    await transactionDone(transaction);
    return count;
  };

  // データ読み取りでは内容を一つだけ解決する。stat・存在確認
  // はこの関数を呼ばずメタデータ索引を使う。起動時には別途容量
  // 制限付きの先読みがあり、メタデータだけの処理とは扱わない。
  const getRuntimeFile = async (path) => {
    path = normalizePath(path);
    if (!path.startsWith("/user/")) return null;

    const mode = resolvedStorageMode !== undefined
      ? resolvedStorageMode : await getStorageMode();
    if (mode === "filesystem") {
      const root = await getFileSystemDirectory();
      if (!root || !(await hasHandlePermission(root, false)))
        throw new Error("Chromeフォルダーへ再接続してください。");
      const parts = path.replace(/^\/user\//, "").split("/").filter(Boolean);
      const name = parts.pop();
      let directory = root;
      for (const part of parts)
        directory = await directory.getDirectoryHandle(part, {create: false});
      return (await directory.getFileHandle(name, {create: false})).getFile();
    }

    const key = storageKey("user", path);
    const cached = runtimeFileCache.get(key);
    if (cached) return cached.promise;
    return queueRuntimeFileRead(key);
  };

  const runtimeScope = async (path) => {
    const normalized = normalizePath(path);
    if (normalized === "/user" || normalized.startsWith("/user/"))
      return (await getStorageMode()) === "filesystem" ? "filesystem" : "user";
    throw new Error(`Path is outside the web storage roots: ${path}`);
  };

  const saveRuntimeFile = async (path, data, mtimeMs = Date.now(), directoryTimes = []) => {
    path = normalizePath(path);
    if (!path.startsWith("/user/") || isTransientPath(path)) return;
    const mode = await getStorageMode();
    const now = Date.now();
    let size = Number(data && (data.size ?? data.byteLength) || 0);
    const scope = mode === "filesystem" ? "filesystem" : "user";
    const statRecords = [
      ...directoryStats(scope, path, now),
      {key: storageKey(scope, path), scope, path, pathKey: lowerPath(path),
       parentKey: storageKey(scope, parentPath(path), true), kind: "file", size,
       mtimeMs: Number(mtimeMs ?? now), updatedAt: now}
    ];
    // ディレクトリーが実際に変わったかは実行環境が把握している。子
    // の上書きで、その更新日時を非同期の確定時刻に置き換えない。
    const ancestorTimes = new Map(directoryTimes.map(row =>
      [lowerPath(row.path, true), Number(row.mtimeMs)]));
    for (const record of statRecords) {
      const timestamp = ancestorTimes.get(record.pathKey);
      if (record.kind === 'directory' && Number.isFinite(timestamp))
        record.mtimeMs = timestamp;
    }
    if (mode === "filesystem") {
      const root = await getFileSystemDirectory();
      if (!root || !(await hasHandlePermission(root, false)))
        throw new Error("Chromeフォルダーへの権限がありません。");
      const parts = path.replace(/^\/user\//, "").split("/").filter(Boolean);
      const name = parts.pop();
      const directory = await resolveDirectory(root, parts);
      const handle = await directory.getFileHandle(name, {create: true});
      const writable = await handle.createWritable();
      await writable.write(data instanceof Blob ? data : new Blob([data]));
      await writable.close();
      const file = await handle.getFile();
      size = file.size;
      statRecords[statRecords.length - 1].size = size;
      await putRecords(openStatDb(), STAT_STORE, statRecords);
    } else {
      const key = storageKey(scope, path);
      await putFileAndStatRecords([{
        key, scope, path, pathKey: lowerPath(path),
        parentKey: storageKey(scope, parentPath(path), true),
        data: data instanceof Blob ? data : new Blob([data], {type: "application/octet-stream"}),
        updatedAt: now
      }], statRecords);
    }
  };

  // メタデータだけの更新で Blob を読み書きしない。特にコピー後
  // の utime は、close が内容を保存済みの後に起き得る。
  const touchRuntimePath = async (path, mtimeMs, directory = false) => {
    path = normalizePath(path, directory);
    if (!path.startsWith('/user/') || isTransientPath(path)) return;
    mtimeMs = Number(mtimeMs);
    if (!Number.isFinite(mtimeMs)) throw new Error('更新日時が不正です');
    const scope = await runtimeScope(path);
    const database = await openStatDb();
    const transaction = database.transaction(STAT_STORE, 'readwrite');
    const store = transaction.objectStore(STAT_STORE);
    const request = store.get(storageKey(scope, path, directory));
    request.onsuccess = () => {
      if (!request.result) { transaction.abort(); return; }
      store.put({...request.result, mtimeMs, updatedAt: Date.now()});
    };
    await transactionDone(transaction);
    invalidateRuntimeStats(scope);
  };

  const deleteRecordsByPrefix = async (databasePromise, storeName, scope, path) => {
    const database = await databasePromise;
    const transaction = database.transaction(storeName, "readwrite");
    const store = transaction.objectStore(storeName);
    const prefix = lowerPath(path).replace(/\/$/, "");
    const start = `${scope}:${prefix}`;
    const request = store.openKeyCursor(IDBKeyRange.bound(start, `${start}/\uffff`));
    request.onsuccess = () => {
      const cursor = request.result;
      if (!cursor) return;
      const key = String(cursor.primaryKey);
      const pathKey = key.slice(scope.length + 1).replace(/\/$/, "");
      if (pathKey === prefix || pathKey.startsWith(`${prefix}/`)) store.delete(cursor.primaryKey);
      cursor.continue();
    };
    await transactionDone(transaction);
    if (storeName === STAT_STORE) {
      invalidateRuntimeStats(scope);
      invalidateRuntimeFileTrees(scope, [path]);
    }
  };

  const deleteRecordsByPrefixFromStore = (transaction, storeName, scope, path) => {
    const store = transaction.objectStore(storeName);
    const prefix = lowerPath(path).replace(/\/$/, "");
    const start = `${scope}:${prefix}`;
    const request = store.openKeyCursor(IDBKeyRange.bound(start, `${start}/\uffff`));
    request.onsuccess = () => {
      const cursor = request.result;
      if (!cursor) return;
      const key = String(cursor.primaryKey);
      const pathKey = key.slice(scope.length + 1).replace(/\/$/, "");
      if (pathKey === prefix || pathKey.startsWith(`${prefix}/`)) store.delete(cursor.primaryKey);
      cursor.continue();
    };
  };

  const deleteFileAndStatByPrefix = async (scope, path) => {
    const database = await openStorageDb();
    const transaction = database.transaction([FILE_STORE, STAT_STORE], "readwrite");
    deleteRecordsByPrefixFromStore(transaction, FILE_STORE, scope, path);
    deleteRecordsByPrefixFromStore(transaction, STAT_STORE, scope, path);
    await transactionDone(transaction);
    invalidateRuntimeStats(scope);
    invalidateRuntimeFileTrees(scope, [path]);
  };

  const deleteRuntimePath = async (path) => {
    path = normalizePath(path);
    if (!path.startsWith("/user/")) return;
    const mode = await getStorageMode();
    if (mode === "filesystem") {
      const root = await getFileSystemDirectory();
      if (!root || !(await hasHandlePermission(root, false)))
        throw new Error("UserDataフォルダーの削除権限がありません。");
      const parts = path.replace(/^\/user\//, "").split("/").filter(Boolean);
      const name = parts.pop();
      const parent = await resolveDirectory(root, parts);
      await parent.removeEntry(name, {recursive: true});
    }
    const scope = mode === "filesystem" ? "filesystem" : "user";
    if (mode === "filesystem")
      await deleteRecordsByPrefix(openStatDb(), STAT_STORE, scope, path);
    else
      await deleteFileAndStatByPrefix(scope, path);
  };

  const makeRuntimeDirectory = async (path) => {
    path = normalizePath(path, true);
    if (!path.startsWith("/user/")) return;
    const mode = await getStorageMode();
    if (mode === "filesystem") {
      const root = await getFileSystemDirectory();
      if (!root || !(await hasHandlePermission(root, false)))
        throw new Error("UserDataフォルダーの作成権限がありません。");
      await resolveDirectory(root, path.replace(/^\/user\//, "").split("/").filter(Boolean));
    }
    const scope = mode === "filesystem" ? "filesystem" : "user";
    await putRecords(openStatDb(), STAT_STORE, directoryStats(scope, `${path}placeholder`, Date.now()));
  };

  const copyFileSystemEntry = async (sourceHandle, destinationParent, destinationName) => {
    if (sourceHandle.kind === "file") {
      const source = await sourceHandle.getFile();
      const destination = await destinationParent.getFileHandle(destinationName, {create: true});
      const writable = await destination.createWritable();
      try {
        await writable.write(source);
        await writable.close();
      } catch (error) {
        if (typeof writable.abort === "function") {
          try { await writable.abort(); } catch (_) { }
        }
        throw error;
      }
      return;
    }
    const destination = await destinationParent.getDirectoryHandle(destinationName, {create: true});
    for await (const [name, child] of sourceHandle.entries()) {
      await copyFileSystemEntry(child, destination, name);
    }
  };

  const fallbackMoveFileSystemEntry = async (
      sourceHandle, oldParent, oldName, newParent, newName, caseOnly) => {
    const recursive = sourceHandle.kind === "directory";
    const randomPart = global.crypto && typeof global.crypto.randomUUID === "function"
      ? global.crypto.randomUUID()
      : `${Date.now()}-${Math.random().toString(16).slice(2)}`;
    const temporaryName = `.cardwirth-move-${randomPart}`;
    let staged = false;
    let sourceRemoved = false;
    try {
      // 準備領域に完全な複製ができるまで入力元を保つ。直接出力
      // 先へコピーすると、最後の削除と保存失敗が競合したとき、
      // 不完全な出力先だけが残り、復旧可能な入力元を失い得る。
      await copyFileSystemEntry(sourceHandle, newParent, temporaryName);
      staged = true;
      const temporaryHandle = recursive
        ? await newParent.getDirectoryHandle(temporaryName)
        : await newParent.getFileHandle(temporaryName);

      if (caseOnly) {
        // 大文字小文字を区別しないホストでは、出力先の表
        // 記が入力元自身を指すため、削除が確定点となる。
        // 確定成功まで準備済み複製を巻き戻し用に保つ。
        await oldParent.removeEntry(oldName, {recursive});
        sourceRemoved = true;
      }

      let committedByMove = false;
      if (typeof temporaryHandle.move === "function") {
        try {
          await temporaryHandle.move(newParent, newName);
          committedByMove = true;
          staged = false;
        } catch (error) {
          if (!error || error.name !== "NotSupportedError") throw error;
        }
      }
      if (!committedByMove) {
        await copyFileSystemEntry(temporaryHandle, newParent, newName);
        await newParent.removeEntry(temporaryName, {recursive});
        staged = false;
      }
      if (!sourceRemoved) {
        await oldParent.removeEntry(oldName, {recursive});
        sourceRemoved = true;
      }
    } catch (error) {
      if (caseOnly && sourceRemoved && staged) {
        // 可能な範囲で元の表記へ復元する。保存プロバイダ
        // ーが巻き戻しも拒否しても、元の失敗を隠さない。
        try {
          const stagedHandle = recursive
            ? await newParent.getDirectoryHandle(temporaryName)
            : await newParent.getFileHandle(temporaryName);
          await copyFileSystemEntry(stagedHandle, oldParent, oldName);
          sourceRemoved = false;
        } catch (rollbackError) {
          const recovery = new Error(`${error.message || error}\n復元失敗: ${rollbackError.message || rollbackError}\n復旧コピー: ${temporaryName}`);
          recovery.name = "RecoveryRequiredError";
          recovery.cause = error;
          recovery.rollbackError = rollbackError;
          recovery.recoveryArtifacts = [{parent: newParent, name: temporaryName}];
          throw recovery;
        }
      }
      throw error;
    } finally {
      if (staged && !sourceRemoved) {
        try { await newParent.removeEntry(temporaryName, {recursive}); } catch (_) { }
      }
    }
  };

  const moveRuntimePath = async (oldPath, newPath) => {
    oldPath = normalizePath(oldPath);
    newPath = normalizePath(newPath);
    if (!oldPath.startsWith("/user/") || !newPath.startsWith("/user/")) return;
    if (oldPath === newPath) return;
    const mode = await getStorageMode();
    const scope = mode === "filesystem" ? "filesystem" : "user";
    const oldStoragePrefix = lowerPath(oldPath).replace(/\/$/, "");
    const newStoragePrefix = lowerPath(newPath).replace(/\/$/, "");
    const caseOnly = oldStoragePrefix === newStoragePrefix;
    if (!caseOnly && newStoragePrefix.startsWith(`${oldStoragePrefix}/`))
      throw new Error("フォルダーを自身の配下へ移動できません。");
    const matchesSource = record => {
      const key = lowerPath(record.path || "").replace(/\/$/, "");
      return key === oldStoragePrefix || key.startsWith(`${oldStoragePrefix}/`);
    };
    const renamedPath = record => {
      // 大小文字の変換で長さが変わり得るため、コード単位ではなくパス要素で切り出す。
      const suffix = normalizePath(record.path).split("/").slice(oldPath.replace(/\/$/, "").split("/").length);
      return `${newPath.replace(/\/$/, "")}${suffix.length ? "/" + suffix.join("/") : ""}`;
    };
    const moveStore = async (databasePromise, storeName) => {
      const records = await recordsForScope(databasePromise, storeName, scope);
      const changed = [];
      const oldPrefix = oldPath.replace(/\/$/, "");
      for (const record of records) {
        const value = String(record.path || "").replace(/\/$/, "");
        if (!matchesSource(record)) continue;
        const directory = record.kind === "directory" || String(record.path).endsWith("/");
        record.path = normalizePath(renamedPath(record), directory);
        record.key = storageKey(scope, record.path, directory);
        record.pathKey = lowerPath(record.path, directory);
        record.parentKey = storageKey(scope, parentPath(record.path), true);
        changed.push(record);
      }
      if (!changed.length) throw new Error("移動元の索引が見つかりません。再索引が必要です。");
      await putRecords(databasePromise, storeName, changed);
    };
    const moveIndexedDbStores = async () => {
      const database = await openStorageDb();
      const transaction = database.transaction([FILE_STORE, STAT_STORE], "readwrite");
      const oldPrefix = oldPath.replace(/\/$/, "");
      for (const storeName of [FILE_STORE, STAT_STORE]) {
        const store = transaction.objectStore(storeName);
        const start = `${scope}:${oldStoragePrefix}`;
        const request = store.openCursor(IDBKeyRange.bound(start, `${start}/\uffff`));
        const changed = [];
        request.onsuccess = () => {
          const cursor = request.result;
          if (!cursor) {
            if (storeName === STAT_STORE && !changed.length) {
              transaction.abort(); // 更新件数ゼロを、名前変更の成功としない。
              return;
            }
            for (const {oldKey, record} of changed) {
              if (!caseOnly) store.delete(oldKey);
              store.put(record);
            }
            return;
          }
          const record = cursor.value;
          const value = String(record.path || "").replace(/\/$/, "");
          if (matchesSource(record)) {
            const oldKey = record.key;
            const directory = record.kind === "directory" || String(record.path).endsWith("/");
            record.path = normalizePath(renamedPath(record), directory);
            record.key = storageKey(scope, record.path, directory);
            record.pathKey = lowerPath(record.path, directory);
            record.parentKey = storageKey(scope, parentPath(record.path), true);
            changed.push({oldKey, record});
          }
          cursor.continue();
        };
      }
      await transactionDone(transaction);
    };
    if (mode === "filesystem") {
      const root = await getFileSystemDirectory();
      if (!root || !(await hasHandlePermission(root, false)))
        throw new Error("Chromeフォルダーへの権限がありません。");
      const oldParts = oldPath.replace(/^\/user\//, "").split("/").filter(Boolean);
      const oldName = oldParts.pop();
      const oldParent = await resolveDirectory(root, oldParts);
      let handle;
      try { handle = await oldParent.getFileHandle(oldName); }
      catch (_) { handle = await oldParent.getDirectoryHandle(oldName); }
      const newParts = newPath.replace(/^\/user\//, "").split("/").filter(Boolean);
      const newName = newParts.pop();
      const newParent = await resolveDirectory(root, newParts);
      let moved = false;
      if (typeof handle.move === "function") {
        try {
          await handle.move(newParent, newName);
          moved = true;
        } catch (error) {
          // 一部の Chromium は move() を公開していても、選択したディレクトリー
          // ハンドルに NotSupportedError を返す。それ以外の失敗は隠さない。
          if (!error || error.name !== "NotSupportedError") throw error;
        }
      }
      if (!moved) await fallbackMoveFileSystemEntry(
        handle, oldParent, oldName, newParent, newName, caseOnly);
      await moveStore(openStatDb(), STAT_STORE);
      if (!caseOnly)
        await deleteRecordsByPrefix(openStatDb(), STAT_STORE, scope, oldPath);
    } else {
      await moveIndexedDbStores();
    }
    invalidateRuntimeStats(scope);
    invalidateRuntimeFileTrees(scope, [oldPath, newPath]);
  };

  // ダウンロードパッケージは UserData へ追加し、queueUserZip
  // と異なり範囲全体を置き換えない。取得した全内容と stat を不可分に確定する。
  const mergeDownloadFiles = async (entries) => {
    if (await getStorageMode() !== "indexeddb") throw new Error("IndexedDB専用です。");
    const seen = new Set(), files = [], stats = new Map();
    let total = 0;
    const now = Date.now();
    if (!entries.length || entries.length > IMPORT_LIMITS.maxEntries) throw new Error("取り込みファイル数が不正です。");
    for (const entry of entries) {
      const path = normalizePath(entry.path);
      if (path !== entry.path || !/^\/user\/(Data|Scenario|Yado|System)\//.test(path) || isTransientPath(path))
        throw new Error(`取り込み先が不正です: ${entry.path}`);
      const key = storageKey("user", path);
      if (seen.has(key)) throw new Error(`取り込み先が重複しています: ${path}`);
      seen.add(key);
      if (!(entry.data instanceof Blob) || entry.data.size > IMPORT_LIMITS.maxFileBytes) throw new Error("ファイルサイズが不正です。");
      total += entry.data.size;
      if (total > IMPORT_LIMITS.maxTotalBytes) throw new Error("展開サイズが上限を超えています。");
      const parentKey = storageKey("user", parentPath(path), true);
      files.push({key, scope:"user", path, pathKey:lowerPath(path), parentKey, data:entry.data, updatedAt:now});
      for (const row of directoryStats("user", path, now)) stats.set(row.key, row);
      stats.set(key, {key, scope:"user", path, pathKey:lowerPath(path), parentKey,
        kind:"file", size:entry.data.size, mtimeMs:now, updatedAt:now});
    }
    await ensureStorageCapacity(total);
    await putFileAndStatRecords(files, [...stats.values()]);
    return files.length;
  };

  const initialize = async ({summary = true} = {}) => {
    if (!initializationPromise) {
      initializationPromise = (async () => {
        await openStorageDb();
        await migrateLegacyDatabases();
      })().catch((error) => {
        initializationPromise = undefined;
        throw error;
      });
    }
    await initializationPromise;
    return summary ? getStorageSummary() : null;
  };

  global.CardWirthWebStorage = Object.freeze({
    initialize,
    deleteAllIndexedDb,
    normalizePath,
    isTransientPath,
    queueUserFolder,
    queueUserZip,
    mergeDownloadFiles,
    applyQueuedUserFolder,
    supportsFileSystemAccess,
    getStorageMode,
    setStorageMode,
    chooseFileSystemDirectory,
    getFileSystemDirectory,
    hasHandlePermission,
    importFileSystemDirectory,
    exportFileSystemDirectory,
    getStorageSummary,
    getRuntimeStats,
    prefetchRuntimeFiles,
    getRuntimeFile,
    getPersistentAsset,
    savePersistentAsset,
    runtimeScope,
    saveRuntimeFile,
    touchRuntimePath,
    deleteRuntimePath,
    moveRuntimePath,
    makeRuntimeDirectory
  });
})(globalThis);
