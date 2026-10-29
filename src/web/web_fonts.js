(function (global) {
  "use strict";

  const DEFAULT_MANIFEST_PATH = "lib/font-manifest.json";
  const MAX_FILES = 32;
  const MAX_FILE_BYTES = 64 * 1024 * 1024;
  const ASSET_KEY_PREFIX = "cardwirth-font-v1:";
  const manifestCache = new Map();
  const validatedAssets = new Map();

  const digest = async (value) => {
    if (!global.crypto || !global.crypto.subtle)
      throw new Error("フォントの整合性確認にWeb Crypto APIが必要です。");
    const bytes = value instanceof ArrayBuffer ? value : await value.arrayBuffer();
    const hash = await global.crypto.subtle.digest("SHA-256", bytes);
    return Array.from(new Uint8Array(hash), byte => byte.toString(16).padStart(2, "0")).join("");
  };

  const validateRelativeFile = (value) => {
    if (typeof value !== "string" || !value || value.startsWith("/") || value.includes("\\") ||
        value.split("/").some(part => !part || part === "." || part === ".."))
      throw new Error("Webフォント配布パスが不正です。");
    return value.normalize("NFC");
  };

  const validateTarget = (value) => {
    if (typeof value !== "string" || !/^\/program\/(?:Font\/)?[^/]+\.(?:otf|ttf)$/i.test(value) ||
        value.includes("\\") || value.split("/").filter(Boolean).some(part => part === "." || part === ".."))
      throw new Error("Webフォントの仮想パスが不正です。");
    return value;
  };

  const validateManifest = (value) => {
    if (!value || value.format !== 1 || !Array.isArray(value.files) ||
        value.files.length === 0 || value.files.length > MAX_FILES)
      throw new Error("Webフォントマニフェストが不正です。");
    const files = [];
    const sourceNames = new Set();
    const targets = new Set();
    let totalBytes = 0;
    for (const item of value.files) {
      const file = validateRelativeFile(item && item.file);
      const target = validateTarget(item && item.target);
      const size = Number(item && item.size);
      const sha256 = String(item && item.sha256 || "").toLowerCase();
      if (sourceNames.has(file) || targets.has(target) || !Number.isSafeInteger(size) ||
          size <= 0 || size > MAX_FILE_BYTES || !/^[0-9a-f]{64}$/.test(sha256))
        throw new Error("Webフォントマニフェストの項目が不正です。");
      sourceNames.add(file);
      targets.add(target);
      totalBytes += size;
      files.push(Object.freeze({file, target, size, sha256}));
    }
    return Object.freeze({format: 1, files: Object.freeze(files), totalBytes});
  };

  const manifestLocation = (manifestPath) => new URL(
    manifestPath || DEFAULT_MANIFEST_PATH,
    global.document?.baseURI || global.location?.href || "http://localhost/");

  const readManifest = async (manifestPath) => {
    const url = manifestLocation(manifestPath);
    const cacheKey = url.href;
    const cached = manifestCache.get(cacheKey);
    if (cached) return cached;
    const promise = (async () => {
      const response = await global.fetch(url);
      if (!response.ok) throw new Error(`Webフォントマニフェスト HTTP ${response.status}`);
      return {url, manifest: validateManifest(await response.json())};
    })();
    manifestCache.set(cacheKey, promise);
    try { return await promise; }
    catch (error) { manifestCache.delete(cacheKey); throw error; }
  };

  const assetKey = (target) => `${ASSET_KEY_PREFIX}${target}`;
  const storage = () => {
    const value = global.CardWirthWebStorage;
    if (!value || typeof value.getPersistentAsset !== "function" ||
        typeof value.savePersistentAsset !== "function")
      throw new Error("Webフォント保存機能を読み込めませんでした。");
    return value;
  };

  const isValidBlob = async (item, blob) => Boolean(blob && blob.size === item.size &&
    await digest(blob) === item.sha256);

  const readCachedAsset = async (item) => {
    const key = assetKey(item.target);
    const signature = `${item.size}:${item.sha256}`;
    const validated = validatedAssets.get(key);
    if (validated && validated.signature === signature) return validated.blob;
    const blob = await storage().getPersistentAsset(key);
    if (!(await isValidBlob(item, blob))) {
      validatedAssets.delete(key);
      return null;
    }
    validatedAssets.set(key, {signature, blob});
    return blob;
  };

  const fetchAsset = async (baseUrl, item) => {
    const response = await global.fetch(new URL(item.file, baseUrl));
    if (!response.ok) throw new Error(`Webフォント ${item.file}: HTTP ${response.status}`);
    const blob = await response.blob();
    if (!(await isValidBlob(item, blob)))
      throw new Error(`Webフォント ${item.file}: サイズまたはSHA-256が一致しません。`);
    return blob;
  };

  const install = async (manifestPath, progress = () => {}) => {
    const {url, manifest} = await readManifest(manifestPath);
    const webStorage = storage();
    if (typeof webStorage.initialize === "function") await webStorage.initialize({summary: false});
    let installed = 0;
    let skipped = 0;
    let bytes = 0;
    for (const [index, item] of manifest.files.entries()) {
      const cached = await readCachedAsset(item);
      if (cached) {
        skipped++;
        bytes += cached.size;
        progress(index + 1, manifest.files.length, item, true);
        continue;
      }
      progress(index, manifest.files.length, item, false);
      const blob = await fetchAsset(url, item);
      await webStorage.savePersistentAsset(assetKey(item.target), blob);
      validatedAssets.set(assetKey(item.target), {
        signature: `${item.size}:${item.sha256}`, blob
      });
      installed++;
      bytes += blob.size;
      progress(index + 1, manifest.files.length, item, false);
    }
    return {files: manifest.files.length, installed, skipped, bytes, totalBytes: manifest.totalBytes};
  };

  const isInstalled = async (manifestPath) => {
    const {manifest} = await readManifest(manifestPath);
    for (const item of manifest.files)
      if (!(await readCachedAsset(item))) return false;
    return true;
  };

  const ensureDirectory = (fs, path) => {
    const parts = path.split("/").filter(Boolean);
    let current = "";
    for (const part of parts) {
      current += `/${part}`;
      try { fs.stat(current); }
      catch (_) { fs.mkdir(current); }
    }
  };

  const prepareFilesystem = async (fs, manifestPath, {requireInstalled = true} = {}) => {
    const {url, manifest} = await readManifest(manifestPath);
    const webStorage = storage();
    if (typeof webStorage.initialize === "function") await webStorage.initialize({summary: false});
    ensureDirectory(fs, "/program");
    ensureDirectory(fs, "/program/Font");
    for (const item of manifest.files) {
      let blob = await readCachedAsset(item);
      if (!blob) {
        if (requireInstalled)
          throw new Error("WebフォントがIndexedDBにありません。起動画面からPy_Cプログラムをインストールしてください。");
        blob = await fetchAsset(url, item);
      }
      fs.writeFile(item.target, new Uint8Array(await blob.arrayBuffer()));
    }
    return {files: manifest.files.length, totalBytes: manifest.totalBytes};
  };

  global.CardWirthWebFonts = Object.freeze({
    install,
    isInstalled,
    prepareFilesystem,
    readManifest
  });
})(globalThis);
