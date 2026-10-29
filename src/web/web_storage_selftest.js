(async function runCardWirthWebStorageSelfTest() {
  "use strict";
  const output = document.getElementById("result");
  const lines = [];
  const write = (value) => {
    lines.push(String(value));
    output.textContent = lines.join("\n");
  };
  const assert = (condition, message) => {
    if (!condition) throw new Error(message);
    write(`OK: ${message}`);
  };
  const expectRejected = async (action, message) => {
    let error = null;
    try { await action(); }
    catch (caught) { error = caught; }
    assert(Boolean(error), message);
    return error;
  };
  try {
    const storage = globalThis.CardWirthWebStorage;
    assert(!!storage, "ストレージAPIを読み込める");
    await storage.deleteAllIndexedDb();

    const createLegacyDatabase = (name, storeName, records) => new Promise((resolve, reject) => {
      const request = indexedDB.open(`${name}-SelfTest`, 1);
      request.onupgradeneeded = () => request.result.createObjectStore(storeName, {keyPath: "key"});
      request.onerror = () => reject(request.error);
      request.onsuccess = () => {
        const database = request.result;
        const transaction = database.transaction(storeName, "readwrite");
        for (const record of records) transaction.objectStore(storeName).put(record);
        transaction.oncomplete = () => { database.close(); resolve(); };
        transaction.onerror = () => { database.close(); reject(transaction.error); };
        transaction.onabort = () => { database.close(); reject(transaction.error); };
      };
    });
    const legacyPath = "/user/Yado/旧版宿/Environment.xml";
    const legacyKey = `user:${legacyPath.toLowerCase()}`;
    await createLegacyDatabase("CardWirthCWebFiles", "files", [{
      key: legacyKey, scope: "user", path: legacyPath,
      data: new Blob(["<Environment><Name>旧版宿</Name></Environment>"])
    }]);
    await createLegacyDatabase("CardWirthCWebStats", "stats", [{
      key: legacyKey, scope: "user", path: legacyPath, pathKey: legacyPath.toLowerCase(),
      parentKey: "user:/user/yado/旧版宿/", kind: "file", size: 49, mtimeMs: 1, updatedAt: 1
    }]);
    await createLegacyDatabase("CardWirthCWebSettings", "settings", [{
      key: "legacy-test-setting", value: "preserved", updatedAt: 1
    }]);
    await storage.initialize();
    const legacyFile = await storage.getRuntimeFile(legacyPath);
    assert(legacyFile instanceof Blob && (await legacyFile.text()).includes("旧版宿"),
      "旧分割DBのUserDataを共有DBへ初回移行できる");
    await storage.saveRuntimeFile(legacyPath, new Blob([
      "<Environment><Name>移行後宿</Name></Environment>"
    ]));
    await storage.initialize();
    const migratedAgain = await storage.getRuntimeFile(legacyPath);
    assert(migratedAgain && (await migratedAgain.text()).includes("移行後宿"),
      "移行済みの共有DBを旧データで再上書きしない");
    const preservedLegacy = await new Promise((resolve, reject) => {
      const request = indexedDB.open("CardWirthCWebFiles-SelfTest", 1);
      request.onerror = () => reject(request.error);
      request.onsuccess = () => {
        const database = request.result;
        const transaction = database.transaction("files", "readonly");
        const get = transaction.objectStore("files").get(legacyKey);
        get.onsuccess = () => { database.close(); resolve(get.result); };
        get.onerror = () => { database.close(); reject(get.error); };
      };
    });
    assert(preservedLegacy && preservedLegacy.data instanceof Blob &&
      (await preservedLegacy.data.text()).includes("旧版宿"),
      "ロールバック用の旧分割DBを移行後も変更しない");
    await storage.setStorageMode("indexeddb");

    const database = await new Promise((resolve, reject) => {
      const request = indexedDB.open("CardWirthPy_CWebStorage-SelfTest", 1);
      request.onsuccess = () => resolve(request.result);
      request.onerror = () => reject(request.error);
    });
    assert(database.version === 1, "共有IndexedDBをversion 1として作成する");
    assert(["files", "settings", "stats"].every((name) =>
      database.objectStoreNames.contains(name)),
    "共有DB内でBlob・stat・設定を別object storeにする");
    const migratedSetting = await new Promise((resolve, reject) => {
      const transaction = database.transaction("settings", "readonly");
      const request = transaction.objectStore("settings").get("legacy-test-setting");
      request.onsuccess = () => resolve(request.result);
      request.onerror = () => reject(request.error);
    });
    assert(migratedSetting && migratedSetting.value === "preserved",
      "旧分割DBの設定を共有DBへ初回移行できる");
    database.close();
    const readStoredPair = async (path) => {
      const db = await new Promise((resolve, reject) => {
        const request = indexedDB.open("CardWirthPy_CWebStorage-SelfTest", 1);
        request.onsuccess = () => resolve(request.result);
        request.onerror = () => reject(request.error);
      });
      try {
        const transaction = db.transaction(["files", "stats"], "readonly");
        const key = `user:${path.normalize("NFC").toLowerCase()}`;
        const payloadRequest = transaction.objectStore("files").get(key);
        const statRequest = transaction.objectStore("stats").get(key);
        return await new Promise((resolve, reject) => {
          transaction.oncomplete = () => resolve({
            payload: payloadRequest.result, stat: statRequest.result
          });
          transaction.onerror = () => reject(transaction.error);
          transaction.onabort = () => reject(transaction.error);
        });
      } finally {
        db.close();
      }
    };

    const zip = new JSZip();
    zip.file("UserData/Yado/自動テスト宿/Environment.xml", "<Environment><Name>自動テスト宿</Name></Environment>");
    zip.file("UserData/Yado/自動テスト宿/Yado.db", new Uint8Array([1, 2, 3, 4]));
    zip.file("UserData/Scenario/説明.txt", "ZIP展開テスト");
    const zipBlob = await zip.generateAsync({type: "blob", compression: "DEFLATE"});
    const imported = await storage.queueUserZip(zipBlob);
    assert(imported === 3, "UserData.zipの3ファイルを展開できる");

    const stats = await storage.getRuntimeStats();
    assert(stats.some((entry) => entry.path === "/user/Yado/自動テスト宿/Environment.xml"),
      "stats storeへ正規化した宿パスを登録できる");
    assert(!stats.some((entry) => entry.path.startsWith("/user/UserData/")),
      "ZIPのUserDataルートを二重に作らない");

    const prefetched = await storage.prefetchRuntimeFiles([
      {path: "/user/Yado/自動テスト宿/Environment.xml", size: 64}
    ]);
    assert(prefetched === 1, "小さなファイルを一括先読みできる");
    const environment = await storage.getRuntimeFile("/user/Yado/自動テスト宿/Environment.xml");
    assert(environment instanceof Blob, "対象ファイル1件をBlobとして遅延取得できる");
    assert((await environment.text()).includes("自動テスト宿"), "遅延取得した内容が一致する");
    const batchFiles = await Promise.all([
      storage.getRuntimeFile("/user/Yado/自動テスト宿/Yado.db"),
      storage.getRuntimeFile("/user/Scenario/説明.txt")
    ]);
    assert(batchFiles.every((file) => file instanceof Blob),
      "同時に要求した複数ファイルを一括取得できる");

    await storage.saveRuntimeFile("/user/Yado/自動テスト宿/save-test.bin", new Uint8Array([9, 8, 7]));
    assert(await storage.getRuntimeFile("/user/Yado/自動テスト宿/Environment.xml") === environment,
      "1ファイルの保存で無関係な読み込みキャッシュを捨てない");
    const saved = await storage.getRuntimeFile("/user/Yado/自動テスト宿/save-test.bin");
    assert(saved && saved.size === 3, "C++書込み相当の保存を再取得できる");
    const savedPair = await readStoredPair("/user/Yado/自動テスト宿/save-test.bin");
    assert(savedPair.payload && savedPair.stat,
      "保存後のBlobとstatが共有DB内に同時に存在する");
    assert(savedPair.payload.data instanceof Blob && !("data" in savedPair.stat),
      "stat recordをBlobなしで単独取得できる");
    await storage.deleteRuntimePath("/user/Yado/自動テスト宿/save-test.bin");
    assert(!(await storage.getRuntimeFile("/user/Yado/自動テスト宿/save-test.bin")),
      "削除をfiles storeへ反映できる");
    assert(await storage.getRuntimeFile("/user/Yado/自動テスト宿/Environment.xml") === environment,
      "削除後も無関係な読み込みキャッシュを維持する");

    await storage.saveRuntimeFile(
      "/user/Yado/自動テスト宿/CaseRename.bin", new Uint8Array([6, 5, 4]));
    await storage.moveRuntimePath(
      "/user/Yado/自動テスト宿/CaseRename.bin",
      "/user/Yado/自動テスト宿/caserename.bin");
    const renamedStats = await storage.getRuntimeStats();
    assert(renamedStats.some((entry) =>
      entry.path === "/user/Yado/自動テスト宿/caserename.bin"),
      "大文字・小文字だけのrenameで同一キーのpayload/statを削除しない");
    const renamed = await storage.getRuntimeFile(
      "/user/Yado/自動テスト宿/caserename.bin");
    assert(renamed && renamed.size === 3, "case-only rename後もファイル本体を取得できる");
    assert(await storage.getRuntimeFile("/user/Yado/自動テスト宿/Environment.xml") === environment,
      "名前変更後も無関係な読み込みキャッシュを維持する");
    await storage.deleteRuntimePath("/user/Yado/自動テスト宿/caserename.bin");

    await storage.saveRuntimeFile("/user/Range/item/深い/日本語.txt", new Blob(["child"]));
    await storage.saveRuntimeFile("/user/Range/item-other.txt", new Blob(["sibling"]));
    await storage.moveRuntimePath("/user/Range/item", "/user/Range/moved");
    assert((await storage.getRuntimeFile("/user/Range/moved/深い/日本語.txt"))?.size === 5,
      "キー範囲限定のrenameで日本語の子孫ファイルを移動できる");
    await storage.deleteRuntimePath("/user/Range/moved");
    assert(!(await storage.getRuntimeFile("/user/Range/moved/深い/日本語.txt")),
      "キー範囲限定の削除で子孫ファイルも削除できる");
    assert((await storage.getRuntimeFile("/user/Range/item-other.txt"))?.size === 7,
      "似た接頭辞の兄弟ファイルをrenameや削除に巻き込まない");

    // 不可分な保存のパターン。名前変更前に入力元・出力先の両方を読み込み済
    // みでも、確定した置換後に古いキャッシュをどちらも残してはならない。
    await storage.saveRuntimeFile("/user/Atomic/save.xml", new Blob(["old"]));
    await storage.saveRuntimeFile("/user/Atomic/save.xml.tmp", new Blob(["new"]));
    await storage.getRuntimeFile("/user/Atomic/save.xml");
    await storage.getRuntimeFile("/user/Atomic/save.xml.tmp");
    await storage.moveRuntimePath("/user/Atomic/save.xml.tmp", "/user/Atomic/save.xml");
    assert((await storage.getRuntimeFile("/user/Atomic/save.xml")) &&
      await (await storage.getRuntimeFile("/user/Atomic/save.xml")).text() === "new",
      "一時ファイルから置換した保存先に古いキャッシュを返さない");
    assert(!(await storage.getRuntimeFile("/user/Atomic/save.xml.tmp")),
      "置換後の一時ファイルのキャッシュを残さない");
    assert(await storage.getRuntimeFile("/user/Yado/自動テスト宿/Environment.xml") === environment,
      "一時ファイルによる置換保存で無関係な読み込みキャッシュを維持する");

    const caseCollision = new JSZip();
    caseCollision.file("UserData/Image/Foo.png", new Uint8Array([1]));
    caseCollision.file("UserData/Image/foo.png", new Uint8Array([2]));
    await expectRejected(
      async () => storage.queueUserZip(await caseCollision.generateAsync({type: "blob"})),
      "大文字・小文字のcase-fold衝突を既存UserDataの置換前に拒否する");

    const unicodeCollision = new JSZip();
    unicodeCollision.file("UserData/Scenario/ガイド.txt", "NFC");
    unicodeCollision.file("UserData/Scenario/ガイド.txt".normalize("NFD"), "NFD");
    await expectRejected(
      async () => storage.queueUserZip(await unicodeCollision.generateAsync({type: "blob"})),
      "NFC／NFD正規化衝突を既存UserDataの置換前に拒否する");

    const longPath = new JSZip();
    longPath.file(`UserData/Scenario/${"a".repeat(1100)}.txt`, "blocked");
    await expectRejected(
      async () => storage.queueUserZip(await longPath.generateAsync({type: "blob"})),
      "安全上限を超えるZIPパス長を拒否する");

    const highRatio = new JSZip();
    highRatio.file("UserData/Scenario/high-ratio.bin", new Uint8Array(2 * 1024 * 1024));
    await expectRejected(
      async () => storage.queueUserZip(await highRatio.generateAsync({type: "blob", compression: "DEFLATE"})),
      "ZIP bomb相当の異常な圧縮率を展開前に拒否する");

    const preserved = await storage.getRuntimeFile("/user/Yado/自動テスト宿/Environment.xml");
    assert(preserved && (await preserved.text()).includes("自動テスト宿"),
      "拒否したZIPが既存UserDataを変更しない");

    const unsafe = new JSZip();
    unsafe.file("../escape.txt", "blocked");
    await expectRejected(
      async () => storage.queueUserZip(await unsafe.generateAsync({type: "blob"})),
      "ZIP Slipパスを拒否する");

    output.className = "pass";
    write("PASS");
    document.title = "PASS - CardWirthPy_C Web Storage Self Test";
  } catch (error) {
    output.className = "fail";
    write(`FAIL: ${error && error.stack ? error.stack : error}`);
    document.title = "FAIL - CardWirthPy_C Web Storage Self Test";
    console.error(error);
  }
})();
